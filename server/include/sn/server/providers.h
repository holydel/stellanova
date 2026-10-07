#pragma once

#include <sn/server/signin.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Sign-ins checked with their providers (docs/adr/0016-sign-in-and-admin.md):
// Steam's Web API for a Steam ticket, Google's and Apple's published keys
// for their ID tokens (RS256), Discord's API for an OAuth code. HTTPS
// through libcurl (Linux) or WinHTTP (Windows), signatures through libcrypto
// or CNG; the calls run on a thread of the checker's own. stellanova-server
// links it (sn::signin); the client's local server does not.
namespace sn::server
{
using ph::i64;
using ph::u32;

// The server's sign-in file (`stellanova-server --signin FILE`): which
// providers it offers, their ids, and their secrets. A provider without its
// ids is off.
struct SignInConfig
{
	// Where providers send the browser back: the game's page.
	std::string redirect;
	struct
	{
		std::string key; // the publisher Web API key
		u32 appId = 1096260;
		std::string identity = "stellanova"; // the clients' ticket's
	} steam;
	struct
	{
		std::string client;
	} google;
	struct
	{
		std::string client; // the Services ID
	} apple;
	struct
	{
		std::string client;
		std::string secret;
	} discord;
};

// {"redirect", "steam": {"key", "appId", "identity"}, "google": {"client"},
// "apple": {"client"}, "discord": {"client", "secret"}}; false (with why)
// when the text is not that.
bool ReadSignInConfig(std::string_view json, SignInConfig& config, std::string& why);

// The offers clients get: each provider a browser can start, with its
// authorization URL (the client adds the state, and a nonce for Google and
// Apple).
std::vector<sim::SignInOffer> OffersOf(const SignInConfig& config);

struct HttpAnswer
{
	int status = 0;
	std::string body;
};

class Http
{
public:
	virtual ~Http() = default;
	// False when no answer came (no connection, a timeout); true with any
	// status otherwise. Headers are "Name: value" lines.
	virtual bool Get(const std::string& url, const std::vector<std::string>& headers,
	                 HttpAnswer& answer) = 0;
	virtual bool Post(const std::string& url, const std::vector<std::string>& headers,
	                  std::string_view body, HttpAnswer& answer) = 0;
};

// This OS's HTTPS; null where there is none.
std::unique_ptr<Http> MakeHttp();

// RS256: whether `signature` signs `message` under the RSA public key of
// `modulus` and `exponent` (big-endian bytes). False where there is no
// crypto library.
bool VerifyRs256(std::string_view modulus, std::string_view exponent, std::string_view message,
                 std::string_view signature);

// base64url, padded or not; false on other characters.
bool Base64UrlDecode(std::string_view text, std::string& bytes);
// For a URL's query: letters, digits and -._~ as they are, the rest %XX.
std::string UrlEncode(std::string_view text);

// A checker that asks the providers over `http`. `clock`: seconds since
// 1970 (empty: the system's).
std::unique_ptr<SignInChecker> MakeProviderChecker(const SignInConfig& config,
                                                   std::unique_ptr<Http> http,
                                                   std::function<i64()> clock = {});
} // namespace sn::server
