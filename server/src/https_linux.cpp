// HTTPS and RS256 on Linux (sn/server/providers.h): libcurl and OpenSSL's
// libcrypto, from the Steam Runtime sysroot at build time and the machine's
// own at run time (libcurl.so.4, libcrypto.so.3).

#include <sn/server/providers.h>

#include <curl/curl.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>

namespace sn::server
{
namespace
{
using ph::usize;

constexpr long TIMEOUT_S = 10;
constexpr usize MAX_ANSWER_BYTES = 1u << 20;

usize Append(char* data, usize size, usize count, void* user)
{
	std::string& body = *static_cast<std::string*>(user);
	const usize bytes = size * count;
	if (body.size() + bytes > MAX_ANSWER_BYTES)
		return 0; // too much: the transfer fails
	body.append(data, bytes);
	return bytes;
}

class Curl final : public Http
{
public:
	// curl_global_init is not thread-safe: the checker is made on the main thread.
	Curl() { curl_global_init(CURL_GLOBAL_DEFAULT); }
	~Curl() override { curl_global_cleanup(); }

	bool Get(const std::string& url, const std::vector<std::string>& headers,
	         HttpAnswer& answer) override
	{
		return Send(url, headers, nullptr, answer);
	}

	bool Post(const std::string& url, const std::vector<std::string>& headers,
	          std::string_view body, HttpAnswer& answer) override
	{
		return Send(url, headers, &body, answer);
	}

private:
	static bool Send(const std::string& url, const std::vector<std::string>& headers,
	                 const std::string_view* body, HttpAnswer& answer)
	{
		answer = {};
		CURL* curl = curl_easy_init();
		if (!curl)
			return false;
		curl_slist* lines = curl_slist_append(nullptr, "Accept: application/json");
		for (const std::string& header : headers)
			lines = curl_slist_append(lines, header.c_str());
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, lines);
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "stellanova-server");
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT_S);
		curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Append);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &answer.body);
		if (body)
		{
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->data());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(body->size()));
		}
		const CURLcode result = curl_easy_perform(curl);
		long status = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
		curl_slist_free_all(lines);
		curl_easy_cleanup(curl);
		answer.status = int(status);
		return result == CURLE_OK;
	}
};
} // namespace

std::unique_ptr<Http> MakeHttp() { return std::make_unique<Curl>(); }

bool VerifyRs256(std::string_view modulus, std::string_view exponent, std::string_view message,
                 std::string_view signature)
{
	const auto bytes = [](std::string_view text)
	{ return reinterpret_cast<const unsigned char*>(text.data()); };
	BIGNUM* n = BN_bin2bn(bytes(modulus), int(modulus.size()), nullptr);
	BIGNUM* e = BN_bin2bn(bytes(exponent), int(exponent.size()), nullptr);
	OSSL_PARAM_BLD* build = OSSL_PARAM_BLD_new();
	OSSL_PARAM* params = nullptr;
	if (n && e && build && OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_N, n) == 1 &&
	    OSSL_PARAM_BLD_push_BN(build, OSSL_PKEY_PARAM_RSA_E, e) == 1)
		params = OSSL_PARAM_BLD_to_param(build);
	EVP_PKEY_CTX* from = params ? EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr) : nullptr;
	EVP_PKEY* key = nullptr;
	const bool made = from && EVP_PKEY_fromdata_init(from) == 1 &&
	                  EVP_PKEY_fromdata(from, &key, EVP_PKEY_PUBLIC_KEY, params) == 1;
	EVP_MD_CTX* digest = made ? EVP_MD_CTX_new() : nullptr;
	const bool verified = digest &&
	                      EVP_DigestVerifyInit(digest, nullptr, EVP_sha256(), nullptr, key) == 1 &&
	                      EVP_DigestVerify(digest, bytes(signature), signature.size(),
	                                       bytes(message), message.size()) == 1;
	EVP_MD_CTX_free(digest);
	EVP_PKEY_free(key);
	EVP_PKEY_CTX_free(from);
	OSSL_PARAM_free(params);
	OSSL_PARAM_BLD_free(build);
	BN_free(e);
	BN_free(n);
	return verified;
}
} // namespace sn::server
