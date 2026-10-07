#include <sn/server/providers.h>

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace sn::server;
using ph::i64;
using ph::u64;

namespace
{
// An RSA key and ID tokens it signed, made with openssl for these tests:
// Google's for client "test-client.apps.googleusercontent.com" with nonce
// "n0nce-1", Apple's for "com.example.stellanova.web" with "n0nce-2"; both
// issued at 1999996400 and expiring at 2000000000.
constexpr const char* TEST_N =
	"uKzpti4a_vE4IFIoMEzquxFnMyJV4TJEAOQmlGloNmw1cWmZEOHB1Kkp7VHxJeK4oKma9-lMIqerTGKP"
	"MDyK8Uqt6PT5q656RQBiM2BYnsR4_4Ovj11prbr_OhRNguofq0quatyAsuqghW0MWNVLKGwQh5_yF4LU"
	"5TmqU7CjZNJcyh7pWroeEbIdPFK1ecvfcaR5Kg0_rv9sbVutPLo5wrv3A74PoW0MVr2GzT9EezQg2vLy"
	"Mird30W6FKD_tA2RnrCmc1-yko55zL5YqKPjSoCXXU7I65Hutr8Bnur3FVBOxavENNT_bD-HiN-3db2Q"
	"d_nB4q_uAYQrEQQk6wCxTQ";
constexpr const char* GOOGLE_TOKEN =
	"eyJhbGciOiJSUzI1NiIsImtpZCI6InRlc3QiLCJ0eXAiOiJKV1QifQ.eyJpc3MiOiJodHRwczovL2FjY"
	"291bnRzLmdvb2dsZS5jb20iLCJhdWQiOiJ0ZXN0LWNsaWVudC5hcHBzLmdvb2dsZXVzZXJjb250ZW50L"
	"mNvbSIsInN1YiI6IjExMDE2OTQ4NDQ3NDM4NjI3NjMzNCIsImV4cCI6MjAwMDAwMDAwMCwiaWF0IjoxO"
	"Tk5OTk2NDAwLCJub25jZSI6Im4wbmNlLTEiLCJuYW1lIjoiUmVhbCBOYW1lIn0.mmsYE92mvB8wEWFlg"
	"9VIYRnQBtheYptJ6jNN9BoiaPWu8KOeqBlILTbGUrjvrM92TjoGzwr18Mqb3okcqBuNe_1k3-XS35Xk1"
	"9WxopRG-yDl4rdTzvxLqHbV2JxzWWsMpZH7uxdJI36LceTC97va74BQY4c4Z3ExenJUGTtNBz3ryjIMW"
	"qy4jwCBKGrMduGyF0rwYSCFQTVxLz9NW7aJYKsIm_wW9_XrZgec9EQP2fwwPRwBrmcD5CJEFyGHrMysF"
	"PpUyIBX6wTZ3TkYTquYHQpHdWcLazn9-uBVccXUArEy0odLEGdVzcyVviH-08l23WSnqJdgg9Ne0YhmB"
	"N8-vg";
constexpr const char* APPLE_TOKEN =
	"eyJhbGciOiJSUzI1NiIsImtpZCI6InRlc3QiLCJ0eXAiOiJKV1QifQ.eyJpc3MiOiJodHRwczovL2Fwc"
	"GxlaWQuYXBwbGUuY29tIiwiYXVkIjoiY29tLmV4YW1wbGUuc3RlbGxhbm92YS53ZWIiLCJzdWIiOiIwM"
	"DEyMzQuYWJjZGVmMDEyMzQ1Njc4OS4wMDQyIiwiZXhwIjoyMDAwMDAwMDAwLCJpYXQiOjE5OTk5OTY0M"
	"DAsIm5vbmNlIjoibjBuY2UtMiJ9.LkS4yXIELIWb0a-vyatbz0jTeKm0pLCiPrIkh3-hALIonMOo785v"
	"qszhy3HqTBWMGi_dXFxWW2As4DEk8VnPVW2Su6k9ptQNBc3bzYKVY7Q7mOdbxi46pL7jz46LczMt7TUH"
	"3a4LDciNMLoSPqv5bXu6fhWU5RLAg7nvW-FxoY-XHbVBikz9fUivAnk9fmnYtmXo57MVT57jdlblSmIQ"
	"n-_14k2wXVD_Ik2Q45qnb4U7hfblWYOvVctWA2jveXPdTYn8Luc8CGZxWMnTJFxDs0rBEab4pmKKzjzw"
	"Fd0fSLHIpwv7UrzQZD4JGqHv2tdiHLXdsSF5YAFh0Ju7HvKREQ";
constexpr i64 TOKEN_TIME = 1'999'999'000;

// Answers by the start of the URL; 0 is no answer at all. Remembers what
// it was asked.
class FakeHttp final : public Http
{
public:
	struct Rule
	{
		std::string prefix;
		int status = 200;
		std::string body;
	};

	FakeHttp(std::vector<Rule> answers, std::vector<std::string>* log)
		: rules(std::move(answers)), asked(log)
	{
	}

	bool Get(const std::string& url, const std::vector<std::string>& headers,
	         HttpAnswer& answer) override
	{
		if (asked)
			asked->push_back("GET " + url + (headers.empty() ? "" : " " + headers.front()));
		return Answer(url, answer);
	}

	bool Post(const std::string& url, const std::vector<std::string>&, std::string_view body,
	          HttpAnswer& answer) override
	{
		if (asked)
			asked->push_back("POST " + url + " " + std::string(body));
		return Answer(url, answer);
	}

private:
	bool Answer(const std::string& url, HttpAnswer& answer)
	{
		for (const Rule& rule : rules)
		{
			if (!url.starts_with(rule.prefix))
				continue;
			if (rule.status == 0)
				return false;
			answer = {rule.status, rule.body};
			return true;
		}
		answer = {404, "{}"};
		return true;
	}

	std::vector<Rule> rules;
	std::vector<std::string>* asked;
};

std::string Keys()
{
	return std::string(R"({"keys":[{"kty":"RSA","kid":"test","alg":"RS256","use":"sig","n":")") +
	       TEST_N + R"(","e":"AQAB"}]})";
}

SignInConfig Config()
{
	SignInConfig config;
	config.redirect = "https://example.com/play.html";
	config.steam.key = "KEY";
	config.google.client = "test-client.apps.googleusercontent.com";
	config.apple.client = "com.example.stellanova.web";
	config.discord.client = "123";
	config.discord.secret = "s3cret";
	return config;
}

std::unique_ptr<SignInChecker> Checker(std::vector<FakeHttp::Rule> rules, i64 now = TOKEN_TIME,
                                       std::vector<std::string>* asked = nullptr,
                                       const SignInConfig& config = Config())
{
	return MakeProviderChecker(config, std::make_unique<FakeHttp>(std::move(rules), asked),
	                           [now] { return now; });
}

// The checker's answer, waited for.
SignInAnswer Ask(SignInChecker& checker, const std::string& provider, const std::string& proof,
                 const std::string& nonce = {})
{
	static u64 next = 0;
	const u64 ticket = ++next;
	checker.Check(ticket, {provider, proof, nonce});
	for (int i = 0; i < 2000; ++i)
	{
		u64 answered = 0;
		SignInAnswer answer;
		if (checker.Poll(answered, answer))
		{
			REQUIRE(answered == ticket);
			return answer;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	FAIL("the checker never answered");
	return {};
}

constexpr const char* STEAM =
	"https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/";
constexpr const char* STEAM_NAMES = "https://partner.steam-api.com/ISteamUser/GetPlayerSummaries";
constexpr const char* STEAM_OK =
	R"({"response":{"params":{"result":"OK","steamid":"76561198000000001",)"
	R"("ownersteamid":"76561198000000001","vacbanned":false,"publisherbanned":false}}})";
} // namespace

TEST_CASE("signin: base64url and URL encoding")
{
	std::string bytes;
	CHECK(Base64UrlDecode("aGVsbG8", bytes));
	CHECK(bytes == "hello");
	CHECK(Base64UrlDecode("aGVsbG8=", bytes));
	CHECK(bytes == "hello");
	CHECK(Base64UrlDecode("-_8", bytes));
	CHECK(bytes == "\xfb\xff");
	CHECK_FALSE(Base64UrlDecode("aGV+bG8", bytes));
	CHECK_FALSE(Base64UrlDecode("a", bytes));
	CHECK(UrlEncode("a b&c=d/~") == "a%20b%26c%3Dd%2F~");
}

TEST_CASE("signin: the sign-in file and its offers")
{
	SignInConfig config;
	std::string why;
	REQUIRE(ReadSignInConfig(R"({"redirect": "https://example.com/play.html",
		"steam": {"key": "KEY", "appId": 480},
		"google": {"client": "g"}, "discord": {"client": "d", "secret": "s"}})",
	                         config, why));
	CHECK(config.steam.key == "KEY");
	CHECK(config.steam.appId == 480);
	CHECK(config.steam.identity == "stellanova");
	const std::vector<sn::sim::SignInOffer> offers = OffersOf(config);
	REQUIRE(offers.size() == 2); // Apple has no client
	CHECK(offers[0].provider == "google");
	CHECK(offers[0].url == "https://accounts.google.com/o/oauth2/v2/auth?client_id=g"
	                       "&redirect_uri=https%3A%2F%2Fexample.com%2Fplay.html"
	                       "&response_type=id_token&scope=openid&prompt=select_account");
	CHECK(offers[1].provider == "discord");
	CHECK_FALSE(ReadSignInConfig(R"({"redirect": "http://example.com/"})", config, why));
	CHECK_FALSE(ReadSignInConfig(R"({"steam": {"key": 5}})", config, why));
	CHECK(why == "steam.key is not text");
	CHECK_FALSE(ReadSignInConfig("not JSON", config, why));
	// Steam alone: no offers for browsers.
	REQUIRE(ReadSignInConfig(R"({"steam": {"key": "KEY"}})", config, why));
	CHECK(OffersOf(config).empty());
}

TEST_CASE("signin: RS256 signatures")
{
	std::string modulus;
	REQUIRE(Base64UrlDecode(TEST_N, modulus));
	const std::string token = GOOGLE_TOKEN;
	const std::string_view signedPart = std::string_view(token).substr(0, token.rfind('.'));
	std::string signature;
	REQUIRE(Base64UrlDecode(std::string_view(token).substr(token.rfind('.') + 1), signature));
	const std::string exponent("\x01\x00\x01", 3);
	CHECK(VerifyRs256(modulus, exponent, signedPart, signature));
	std::string changed(signedPart);
	changed.back() ^= 1;
	CHECK_FALSE(VerifyRs256(modulus, exponent, changed, signature));
	signature[10] ^= 1;
	CHECK_FALSE(VerifyRs256(modulus, exponent, signedPart, signature));
}

TEST_CASE("signin: Steam tickets")
{
	std::vector<std::string> asked;
	auto checker =
		Checker({{STEAM, 200, STEAM_OK},
		         {STEAM_NAMES, 200, R"({"response":{"players":[{"personaname":"Ann"}]}})"}},
		        TOKEN_TIME, &asked);
	SignInAnswer answer = Ask(*checker, "steam", "0a1b2c");
	CHECK(answer.error.empty());
	CHECK(answer.provider == "steam");
	CHECK(answer.id == "76561198000000001");
	CHECK(answer.name == "Ann");
	REQUIRE(!asked.empty());
	CHECK(asked[0] ==
	      std::string("GET ") + STEAM + "?key=KEY&appid=1096260&ticket=0a1b2c&identity=stellanova");
	// The same ticket twice: no.
	CHECK(Ask(*checker, "steam", "0a1b2c").error == "error.signin_failed");
	// Not hex.
	CHECK(Ask(*checker, "steam", "xyz1").error == "error.signin_failed");

	auto refusing = Checker(
		{{STEAM, 200, R"({"response":{"error":{"errorcode":101,"errordesc":"Invalid ticket"}}})"}});
	CHECK(Ask(*refusing, "steam", "0a1b").error == "error.signin_failed");
	auto banning =
		Checker({{STEAM, 200,
		          R"({"response":{"params":{"result":"OK","steamid":"76561198000000001",)"
		          R"("publisherbanned":true}}})"}});
	CHECK(Ask(*banning, "steam", "0a1b").error == "error.signin_banned");
	auto away = Checker({{STEAM, 0, ""}});
	CHECK(Ask(*away, "steam", "0a1b").error == "error.signin_unreachable");
	SignInConfig noKey = Config();
	noKey.steam.key.clear();
	auto off = Checker({}, TOKEN_TIME, nullptr, noKey);
	CHECK(Ask(*off, "steam", "0a1b").error == "error.signin_off");
	CHECK(Ask(*off, "nobody", "0a1b").error == "error.signin_off");
}

TEST_CASE("signin: Google's and Apple's ID tokens")
{
	const std::vector<FakeHttp::Rule> keys = {
		{"https://www.googleapis.com/oauth2/v3/certs", 200, Keys()},
		{"https://appleid.apple.com/auth/keys", 200, Keys()}};
	auto checker = Checker(keys);
	SignInAnswer answer = Ask(*checker, "google", GOOGLE_TOKEN, "n0nce-1");
	CHECK(answer.error.empty());
	CHECK(answer.id == "110169484474386276334");
	CHECK(answer.name.empty()); // Google's name is never taken
	CHECK(Ask(*checker, "google", GOOGLE_TOKEN, "n0nce-1").error == "error.signin_failed");
	answer = Ask(*checker, "apple", APPLE_TOKEN, "n0nce-2");
	CHECK(answer.error.empty());
	CHECK(answer.id == "001234.abcdef0123456789.0042");

	auto fresh = Checker(keys);
	// Another nonce, another provider's token, another client.
	CHECK(Ask(*fresh, "google", GOOGLE_TOKEN, "other").error == "error.signin_failed");
	CHECK(Ask(*fresh, "google", GOOGLE_TOKEN).error == "error.signin_failed");
	CHECK(Ask(*fresh, "apple", GOOGLE_TOKEN, "n0nce-1").error == "error.signin_failed");
	SignInConfig otherClient = Config();
	otherClient.google.client = "someone-else";
	auto foreign = Checker(keys, TOKEN_TIME, nullptr, otherClient);
	CHECK(Ask(*foreign, "google", GOOGLE_TOKEN, "n0nce-1").error == "error.signin_failed");
	// Expired.
	auto late = Checker(keys, 2'000'000'000 + 3600);
	CHECK(Ask(*late, "google", GOOGLE_TOKEN, "n0nce-1").error == "error.signin_failed");
	// A changed payload breaks the signature.
	std::string forged = GOOGLE_TOKEN;
	char& letter = forged[forged.find('.') + 5];
	letter = letter == 'A' ? 'B' : 'A';
	CHECK(Ask(*fresh, "google", forged, "n0nce-1").error == "error.signin_failed");
	// The keys cannot be fetched.
	auto away = Checker({});
	CHECK(Ask(*away, "google", GOOGLE_TOKEN, "n0nce-1").error == "error.signin_unreachable");
}

TEST_CASE("signin: Discord codes")
{
	std::vector<std::string> asked;
	auto checker = Checker({{"https://discord.com/api/oauth2/token", 200,
	                         R"({"access_token":"t0ken","token_type":"Bearer"})"},
	                        {"https://discord.com/api/users/@me", 200,
	                         R"({"id":"80351110224678912","username":"ann","global_name":"Ann"})"}},
	                       TOKEN_TIME, &asked);
	const SignInAnswer answer = Ask(*checker, "discord", "c0de");
	CHECK(answer.error.empty());
	CHECK(answer.id == "80351110224678912");
	CHECK(answer.name == "Ann");
	REQUIRE(asked.size() == 2);
	CHECK(asked[0] == "POST https://discord.com/api/oauth2/token grant_type=authorization_code"
	                  "&code=c0de&redirect_uri=https%3A%2F%2Fexample.com%2Fplay.html"
	                  "&client_id=123&client_secret=s3cret");
	CHECK(asked[1] == "GET https://discord.com/api/users/@me Authorization: Bearer t0ken");
	auto refusing =
		Checker({{"https://discord.com/api/oauth2/token", 400, R"({"error":"invalid_grant"})"}});
	CHECK(Ask(*refusing, "discord", "c0de").error == "error.signin_failed");
}
