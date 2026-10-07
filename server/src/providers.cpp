#include <sn/server/providers.h>

#include <ph/core/log.h>

#include <yyjson.h>

#include <algorithm>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <initializer_list>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sn::server
{
namespace
{
using ph::u8;
using ph::usize;

constexpr i64 SKEW = 120;           // s a token's times may be off by
constexpr i64 KEYS_KEPT = 3600;     // s a provider's published keys are trusted
constexpr i64 KEYS_RETRY = 60;      // s between fetches for a key nobody knows
constexpr i64 USED_KEPT = 2 * 3600; // s a proof is remembered: it cannot come twice
constexpr usize MAX_USED = 100'000;
constexpr usize MAX_QUEUED = 256;
constexpr usize MIN_RSA_BYTES = 256; // 2048 bits

constexpr const char* STEAM_AUTH =
	"https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/";
constexpr const char* STEAM_SUMMARIES =
	"https://partner.steam-api.com/ISteamUser/GetPlayerSummaries/v2/";
constexpr const char* GOOGLE_AUTHORIZE = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char* GOOGLE_KEYS = "https://www.googleapis.com/oauth2/v3/certs";
constexpr const char* APPLE_AUTHORIZE = "https://appleid.apple.com/auth/authorize";
constexpr const char* APPLE_KEYS = "https://appleid.apple.com/auth/keys";
constexpr const char* DISCORD_AUTHORIZE = "https://discord.com/oauth2/authorize";
constexpr const char* DISCORD_TOKEN = "https://discord.com/api/oauth2/token";
constexpr const char* DISCORD_USER = "https://discord.com/api/users/@me";

// The answers' strings keys.
constexpr const char* OFF = "error.signin_off";
constexpr const char* FAILED = "error.signin_failed";
constexpr const char* UNREACHABLE = "error.signin_unreachable";
constexpr const char* BANNED = "error.signin_banned";

class Json
{
public:
	explicit Json(std::string_view text) : document(yyjson_read(text.data(), text.size(), 0)) {}
	~Json() { yyjson_doc_free(document); }
	Json(const Json&) = delete;
	Json& operator=(const Json&) = delete;
	yyjson_val* Root() const { return document ? yyjson_doc_get_root(document) : nullptr; }

private:
	yyjson_doc* document;
};

yyjson_val* Get(yyjson_val* object, const char* key)
{
	return yyjson_is_obj(object) ? yyjson_obj_get(object, key) : nullptr;
}

std::string_view Text(yyjson_val* value)
{
	return yyjson_is_str(value) ? std::string_view(yyjson_get_str(value), yyjson_get_len(value))
	                            : std::string_view();
}

std::string_view Text(yyjson_val* object, const char* key) { return Text(Get(object, key)); }

i64 Integer(yyjson_val* object, const char* key)
{
	yyjson_val* value = Get(object, key);
	return yyjson_is_num(value) ? i64(yyjson_get_num(value)) : 0;
}

bool Digits(std::string_view text, usize most)
{
	return !text.empty() && text.size() <= most &&
	       std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool IsHex(std::string_view text)
{
	return std::all_of(
		text.begin(), text.end(), [](char c)
		{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

// What OAuth codes, IDs and tokens are made of.
bool IsTokenText(std::string_view text, usize most)
{
	return !text.empty() && text.size() <= most &&
	       std::all_of(text.begin(), text.end(),
	                   [](char c)
	                   {
						   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
						          (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.';
					   });
}

ph::u64 Hash(std::string_view a, std::string_view b)
{
	ph::u64 hash = 14695981039346656037ull; // FNV-1a
	for (const char c : a)
		hash = (hash ^ u8(c)) * 1099511628211ull;
	hash = (hash ^ u8(':')) * 1099511628211ull;
	for (const char c : b)
		hash = (hash ^ u8(c)) * 1099511628211ull;
	return hash;
}

SignInAnswer Fail(const char* why)
{
	SignInAnswer answer;
	answer.error = why;
	return answer;
}

// A provider's published RSA keys (a JWKS), by their kid.
struct KeySet
{
	std::map<std::string, std::pair<std::string, std::string>, std::less<>> keys; // n, e
	i64 fetched = 0;
	i64 tried = 0;
};

class ProviderChecker final : public SignInChecker
{
public:
	ProviderChecker(const SignInConfig& desc, std::unique_ptr<Http> client,
	                std::function<i64()> time)
		: config(desc), http(std::move(client)), clock(std::move(time)), offers(OffersOf(desc))
	{
		worker = std::thread([this] { Work(); });
	}

	~ProviderChecker() override
	{
		{
			const std::lock_guard lock(mutex);
			stopping = true;
		}
		wake.notify_all();
		worker.join();
	}

	std::vector<sim::SignInOffer> Offers() const override { return offers; }

	void Check(u64 ticket, const SignInProof& proof) override
	{
		{
			const std::lock_guard lock(mutex);
			if (queue.size() >= MAX_QUEUED)
			{
				SignInAnswer answer = Fail(UNREACHABLE);
				answer.provider = proof.provider;
				answers.emplace_back(ticket, std::move(answer));
				return;
			}
			queue.emplace_back(ticket, proof);
		}
		wake.notify_one();
	}

	bool Poll(u64& ticket, SignInAnswer& answer) override
	{
		const std::lock_guard lock(mutex);
		if (answers.empty())
			return false;
		ticket = answers.front().first;
		answer = std::move(answers.front().second);
		answers.pop_front();
		return true;
	}

private:
	i64 Now() const { return clock ? clock() : i64(std::time(nullptr)); }

	void Work()
	{
		for (;;)
		{
			std::pair<u64, SignInProof> job;
			{
				std::unique_lock lock(mutex);
				wake.wait(lock, [this] { return stopping || !queue.empty(); });
				if (stopping)
					return;
				job = std::move(queue.front());
				queue.pop_front();
			}
			SignInAnswer answer = CheckNow(job.second);
			answer.provider = job.second.provider;
			if (!answer.error.empty())
				PH_LOG_INFO("signin: %s: %s", job.second.provider.c_str(), answer.error.c_str());
			const std::lock_guard lock(mutex);
			answers.emplace_back(job.first, std::move(answer));
		}
	}

	SignInAnswer CheckNow(const SignInProof& proof)
	{
		if (!http)
			return Fail(OFF);
		// A proof counts once: whoever saw it go by cannot sign in with it.
		const i64 now = Now();
		const ph::u64 hash = Hash(proof.provider, proof.proof);
		std::erase_if(used, [now](const auto& entry) { return entry.second + USED_KEPT < now; });
		if (used.size() >= MAX_USED)
			used.clear(); // a flood: forget, rather than refuse everyone
		if (used.contains(hash))
			return Fail(FAILED);
		SignInAnswer answer;
		if (proof.provider == "steam")
			answer = CheckSteam(proof);
		else if (proof.provider == "google")
			answer = CheckIdToken(proof, config.google.client, GOOGLE_KEYS, google,
			                      {"https://accounts.google.com", "accounts.google.com"});
		else if (proof.provider == "apple")
			answer = CheckIdToken(proof, config.apple.client, APPLE_KEYS, apple,
			                      {"https://appleid.apple.com"});
		else if (proof.provider == "discord")
			answer = CheckDiscord(proof);
		else
			answer = Fail(OFF);
		if (answer.error.empty())
			used[hash] = now;
		return answer;
	}

	// The ticket goes to Steam's Web API, under the identity the clients ask
	// their tickets for; then the persona name.
	SignInAnswer CheckSteam(const SignInProof& proof)
	{
		if (config.steam.key.empty())
			return Fail(OFF);
		if (proof.proof.empty() || proof.proof.size() % 2 || !IsHex(proof.proof))
			return Fail(FAILED);
		HttpAnswer reply;
		const std::string url = std::string(STEAM_AUTH) + "?key=" + UrlEncode(config.steam.key) +
		                        "&appid=" + std::to_string(config.steam.appId) +
		                        "&ticket=" + proof.proof +
		                        "&identity=" + UrlEncode(config.steam.identity);
		if (!http->Get(url, {}, reply) || reply.status >= 500)
			return Fail(UNREACHABLE);
		const Json json(reply.body);
		yyjson_val* response = Get(json.Root(), "response");
		yyjson_val* params = Get(response, "params");
		if (Text(params, "result") != "OK")
		{
			yyjson_val* error = Get(response, "error");
			PH_LOG_INFO("signin: Steam said HTTP %d, error %d (%.*s)", reply.status,
			            int(Integer(error, "errorcode")), int(Text(error, "errordesc").size()),
			            Text(error, "errordesc").data());
			return Fail(FAILED);
		}
		SignInAnswer answer;
		answer.id = Text(params, "steamid");
		if (!Digits(answer.id, 20))
			return Fail(FAILED);
		if (yyjson_get_bool(Get(params, "publisherbanned")))
			return Fail(BANNED);
		HttpAnswer summary;
		if (http->Get(std::string(STEAM_SUMMARIES) + "?key=" + UrlEncode(config.steam.key) +
		                  "&steamids=" + answer.id,
		              {}, summary) &&
		    summary.status == 200)
		{
			const Json players(summary.body);
			yyjson_val* list = Get(Get(players.Root(), "response"), "players");
			answer.name = Text(yyjson_arr_get(list, 0), "personaname");
		}
		return answer;
	}

	// The key `kid` from a provider's published set, fetched again once it
	// is old, or when a token names a key the set lacks (not more than once a
	// minute).
	const std::pair<std::string, std::string>* FindKey(KeySet& set, const char* url,
	                                                   std::string_view kid)
	{
		const i64 now = Now();
		auto found = set.keys.find(kid);
		const bool stale = now - set.fetched > KEYS_KEPT;
		if ((found == set.keys.end() || stale) && now - set.tried >= KEYS_RETRY)
		{
			set.tried = now;
			HttpAnswer reply;
			if (http->Get(url, {}, reply) && reply.status == 200)
			{
				const Json json(reply.body);
				usize index = 0;
				usize max = 0;
				yyjson_val* key = nullptr;
				std::map<std::string, std::pair<std::string, std::string>, std::less<>> keys;
				yyjson_arr_foreach(Get(json.Root(), "keys"), index, max, key)
				{
					std::string n;
					std::string e;
					if (Text(key, "kty") == "RSA" && Base64UrlDecode(Text(key, "n"), n) &&
					    Base64UrlDecode(Text(key, "e"), e) && !Text(key, "kid").empty())
						keys[std::string(Text(key, "kid"))] = {std::move(n), std::move(e)};
				}
				if (!keys.empty())
				{
					set.keys = std::move(keys);
					set.fetched = now;
				}
			}
			found = set.keys.find(kid);
		}
		if (found == set.keys.end() || now - set.fetched > 2 * KEYS_KEPT)
			return nullptr;
		return &found->second;
	}

	// An OpenID Connect ID token: signed with one of the provider's keys
	// (RS256), from its issuer, for our client, not expired, with the nonce
	// the browser sent.
	SignInAnswer CheckIdToken(const SignInProof& proof, const std::string& client,
	                          const char* keysUrl, KeySet& set,
	                          std::initializer_list<std::string_view> issuers)
	{
		if (client.empty())
			return Fail(OFF);
		const std::string_view token = proof.proof;
		const usize first = token.find('.');
		const usize second = first == std::string_view::npos ? first : token.find('.', first + 1);
		if (second == std::string_view::npos || !IsTokenText(token, sim::MAX_PROOF_BYTES))
			return Fail(FAILED);
		std::string headerText;
		std::string payloadText;
		std::string signature;
		if (!Base64UrlDecode(token.substr(0, first), headerText) ||
		    !Base64UrlDecode(token.substr(first + 1, second - first - 1), payloadText) ||
		    !Base64UrlDecode(token.substr(second + 1), signature))
			return Fail(FAILED);
		const Json header(headerText);
		const Json payload(payloadText);
		if (Text(header.Root(), "alg") != "RS256")
			return Fail(FAILED);
		const auto* key = FindKey(set, keysUrl, Text(header.Root(), "kid"));
		if (!key)
			return Fail(set.keys.empty() ? UNREACHABLE : FAILED);
		if (key->first.size() < MIN_RSA_BYTES ||
		    !VerifyRs256(key->first, key->second, token.substr(0, second), signature))
			return Fail(FAILED);
		yyjson_val* claims = payload.Root();
		const std::string_view issuer = Text(claims, "iss");
		if (std::find(issuers.begin(), issuers.end(), issuer) == issuers.end())
			return Fail(FAILED);
		yyjson_val* audience = Get(claims, "aud");
		bool ours = Text(audience) == client;
		usize index = 0;
		usize max = 0;
		yyjson_val* entry = nullptr;
		yyjson_arr_foreach(audience, index, max, entry) { ours = ours || Text(entry) == client; }
		const i64 now = Now();
		if (!ours || Integer(claims, "exp") + SKEW < now || Integer(claims, "iat") - SKEW > now)
			return Fail(FAILED);
		if (proof.nonce.empty() || Text(claims, "nonce") != proof.nonce)
			return Fail(FAILED);
		SignInAnswer answer;
		answer.id = Text(claims, "sub");
		if (!IsTokenText(answer.id, 255))
			return Fail(FAILED);
		return answer; // no name: Google's is often a real one, Apple gives none
	}

	// The code, traded for a token with the application's secret; the token
	// for the user.
	SignInAnswer CheckDiscord(const SignInProof& proof)
	{
		if (config.discord.client.empty() || config.discord.secret.empty() ||
		    config.redirect.empty())
			return Fail(OFF);
		if (!IsTokenText(proof.proof, 256))
			return Fail(FAILED);
		const std::string body = "grant_type=authorization_code&code=" + UrlEncode(proof.proof) +
		                         "&redirect_uri=" + UrlEncode(config.redirect) +
		                         "&client_id=" + UrlEncode(config.discord.client) +
		                         "&client_secret=" + UrlEncode(config.discord.secret);
		HttpAnswer reply;
		if (!http->Post(DISCORD_TOKEN, {"Content-Type: application/x-www-form-urlencoded"}, body,
		                reply) ||
		    reply.status >= 500)
			return Fail(UNREACHABLE);
		const Json grant(reply.body);
		const std::string_view token = Text(grant.Root(), "access_token");
		if (reply.status != 200 || !IsTokenText(token, 512))
			return Fail(FAILED);
		HttpAnswer user;
		if (!http->Get(DISCORD_USER, {"Authorization: Bearer " + std::string(token)}, user) ||
		    user.status >= 500)
			return Fail(UNREACHABLE);
		const Json json(user.body);
		SignInAnswer answer;
		answer.id = Text(json.Root(), "id");
		if (user.status != 200 || !Digits(answer.id, 24))
			return Fail(FAILED);
		answer.name = Text(json.Root(), "global_name");
		if (answer.name.empty())
			answer.name = Text(json.Root(), "username");
		return answer;
	}

	const SignInConfig config;
	const std::unique_ptr<Http> http;
	const std::function<i64()> clock;
	const std::vector<sim::SignInOffer> offers;
	// The worker's own.
	KeySet google;
	KeySet apple;
	std::unordered_map<ph::u64, i64> used;
	// Shared with the worker.
	std::mutex mutex;
	std::condition_variable wake;
	std::deque<std::pair<u64, SignInProof>> queue;
	std::deque<std::pair<u64, SignInAnswer>> answers;
	bool stopping = false;
	std::thread worker;
};

bool ReadText(yyjson_val* object, const char* key, std::string& into, std::string& why,
              const char* where)
{
	yyjson_val* value = Get(object, key);
	if (!value)
		return true;
	if (!yyjson_is_str(value))
	{
		why = std::string(where) + "." + key + " is not text";
		return false;
	}
	into = Text(value);
	return true;
}
} // namespace

bool ReadSignInConfig(std::string_view text, SignInConfig& config, std::string& why)
{
	const Json json(text);
	yyjson_val* root = json.Root();
	if (!yyjson_is_obj(root))
	{
		why = "not a JSON object";
		return false;
	}
	config = {};
	yyjson_val* steam = Get(root, "steam");
	yyjson_val* google = Get(root, "google");
	yyjson_val* apple = Get(root, "apple");
	yyjson_val* discord = Get(root, "discord");
	if (!ReadText(root, "redirect", config.redirect, why, "file") ||
	    !ReadText(steam, "key", config.steam.key, why, "steam") ||
	    !ReadText(steam, "identity", config.steam.identity, why, "steam") ||
	    !ReadText(google, "client", config.google.client, why, "google") ||
	    !ReadText(apple, "client", config.apple.client, why, "apple") ||
	    !ReadText(discord, "client", config.discord.client, why, "discord") ||
	    !ReadText(discord, "secret", config.discord.secret, why, "discord"))
		return false;
	if (yyjson_val* app = Get(steam, "appId"))
	{
		if (!yyjson_is_uint(app) || yyjson_get_uint(app) > 0xffffffffull)
		{
			why = "steam.appId is not an app id";
			return false;
		}
		config.steam.appId = u32(yyjson_get_uint(app));
	}
	const std::string_view redirect = config.redirect;
	if (!redirect.empty() && !redirect.starts_with("https://") &&
	    !redirect.starts_with("http://127.0.0.1") && !redirect.starts_with("http://localhost"))
	{
		why = "redirect is not an https:// address";
		return false;
	}
	return true;
}

std::vector<sim::SignInOffer> OffersOf(const SignInConfig& config)
{
	std::vector<sim::SignInOffer> offers;
	if (config.redirect.empty())
		return offers;
	const std::string back = "&redirect_uri=" + UrlEncode(config.redirect);
	if (!config.google.client.empty())
		offers.push_back({"google", std::string(GOOGLE_AUTHORIZE) +
		                                "?client_id=" + UrlEncode(config.google.client) + back +
		                                "&response_type=id_token&scope=openid"
		                                "&prompt=select_account"});
	if (!config.apple.client.empty())
		offers.push_back({"apple", std::string(APPLE_AUTHORIZE) +
		                               "?client_id=" + UrlEncode(config.apple.client) + back +
		                               "&response_type=code%20id_token&response_mode=fragment"});
	if (!config.discord.client.empty() && !config.discord.secret.empty())
		offers.push_back({"discord", std::string(DISCORD_AUTHORIZE) +
		                                 "?client_id=" + UrlEncode(config.discord.client) + back +
		                                 "&response_type=code&scope=identify&prompt=none"});
	return offers;
}

bool Base64UrlDecode(std::string_view text, std::string& bytes)
{
	while (!text.empty() && text.back() == '=')
		text.remove_suffix(1);
	if (text.size() % 4 == 1)
		return false;
	bytes.clear();
	bytes.reserve(text.size() * 3 / 4);
	ph::u32 bits = 0;
	int count = 0;
	for (const char c : text)
	{
		int value = -1;
		if (c >= 'A' && c <= 'Z')
			value = c - 'A';
		else if (c >= 'a' && c <= 'z')
			value = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			value = c - '0' + 52;
		else if (c == '-')
			value = 62;
		else if (c == '_')
			value = 63;
		if (value < 0)
			return false;
		bits = bits << 6 | ph::u32(value);
		count += 6;
		if (count >= 8)
		{
			count -= 8;
			bytes += char(u8(bits >> count));
		}
	}
	return true;
}

std::string UrlEncode(std::string_view text)
{
	std::string out;
	for (const char c : text)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '.' || c == '_' || c == '~')
		{
			out += c;
			continue;
		}
		out += '%';
		out += "0123456789ABCDEF"[u8(c) >> 4];
		out += "0123456789ABCDEF"[u8(c) & 15];
	}
	return out;
}

std::unique_ptr<SignInChecker> MakeProviderChecker(const SignInConfig& config,
                                                   std::unique_ptr<Http> http,
                                                   std::function<i64()> clock)
{
	return std::make_unique<ProviderChecker>(config, std::move(http), std::move(clock));
}

#ifdef SN_NO_HTTPS
std::unique_ptr<Http> MakeHttp() { return nullptr; }

bool VerifyRs256(std::string_view, std::string_view, std::string_view, std::string_view)
{
	return false;
}
#endif
} // namespace sn::server
