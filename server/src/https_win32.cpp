// HTTPS and RS256 on Windows (sn/server/providers.h): WinHTTP and CNG, for a
// stellanova-server run on the development PC, and the tests.

#include <sn/server/providers.h>

#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include <windows.h>
// After windows.h.
#include <bcrypt.h>
#include <winhttp.h>

#include <cstring>
#include <vector>

namespace sn::server
{
namespace
{
using ph::usize;

constexpr int TIMEOUT_MS = 10'000;
constexpr usize MAX_ANSWER_BYTES = 1u << 20;

std::wstring Widen(const std::string& text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
	std::wstring wide(usize(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), length);
	return wide;
}

struct Handle
{
	HINTERNET handle = nullptr;
	explicit Handle(HINTERNET opened) : handle(opened) {}
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	~Handle()
	{
		if (handle)
			WinHttpCloseHandle(handle);
	}
};

class WinHttp final : public Http
{
public:
	WinHttp()
		: session(WinHttpOpen(L"stellanova-server", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0))
	{
		if (session.handle)
			WinHttpSetTimeouts(session.handle, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);
	}

	bool Get(const std::string& url, const std::vector<std::string>& headers,
	         HttpAnswer& answer) override
	{
		return Send(L"GET", url, headers, {}, answer);
	}

	bool Post(const std::string& url, const std::vector<std::string>& headers,
	          std::string_view body, HttpAnswer& answer) override
	{
		return Send(L"POST", url, headers, body, answer);
	}

private:
	bool Send(const wchar_t* method, const std::string& url,
	          const std::vector<std::string>& headers, std::string_view body, HttpAnswer& answer)
	{
		answer = {};
		const std::wstring wide = Widen(url);
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);
		parts.dwHostNameLength = DWORD(-1);
		parts.dwUrlPathLength = DWORD(-1);
		parts.dwExtraInfoLength = DWORD(-1);
		if (!session.handle || !WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &parts) ||
		    parts.nScheme != INTERNET_SCHEME_HTTPS)
			return false;
		const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
		const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
		                          std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
		const Handle connection(WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
		const Handle request(
			connection.handle ? WinHttpOpenRequest(connection.handle, method, path.c_str(), nullptr,
			                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
			                                       WINHTTP_FLAG_SECURE)
			                  : nullptr);
		if (!request.handle)
			return false;
		std::wstring lines = L"Accept: application/json\r\n";
		for (const std::string& header : headers)
			lines += Widen(header) + L"\r\n";
		void* data = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
		const DWORD size = DWORD(body.size());
		if (!WinHttpSendRequest(request.handle, lines.c_str(), DWORD(lines.size()), data, size,
		                        size, 0) ||
		    !WinHttpReceiveResponse(request.handle, nullptr))
			return false;
		DWORD status = 0;
		DWORD length = sizeof(status);
		WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
		                    WINHTTP_NO_HEADER_INDEX);
		for (;;)
		{
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request.handle, &available))
				return false;
			if (available == 0)
				break;
			if (answer.body.size() + available > MAX_ANSWER_BYTES)
				return false;
			const usize offset = answer.body.size();
			answer.body.resize(offset + available);
			DWORD read = 0;
			if (!WinHttpReadData(request.handle, answer.body.data() + offset, available, &read))
				return false;
			answer.body.resize(offset + read);
		}
		answer.status = int(status);
		return true;
	}

	Handle session;
};

struct Algorithm
{
	BCRYPT_ALG_HANDLE handle = nullptr;
	explicit Algorithm(const wchar_t* name)
	{
		if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&handle, name, nullptr, 0)))
			handle = nullptr;
	}
	Algorithm(const Algorithm&) = delete;
	Algorithm& operator=(const Algorithm&) = delete;
	~Algorithm()
	{
		if (handle)
			BCryptCloseAlgorithmProvider(handle, 0);
	}
};
} // namespace

std::unique_ptr<Http> MakeHttp() { return std::make_unique<WinHttp>(); }

bool VerifyRs256(std::string_view modulus, std::string_view exponent, std::string_view message,
                 std::string_view signature)
{
	while (!modulus.empty() && modulus.front() == '\0')
		modulus.remove_prefix(1);
	while (!exponent.empty() && exponent.front() == '\0')
		exponent.remove_prefix(1);
	if (modulus.empty() || exponent.empty() || signature.size() != modulus.size())
		return false;
	const Algorithm sha256(BCRYPT_SHA256_ALGORITHM);
	const Algorithm rsa(BCRYPT_RSA_ALGORITHM);
	UCHAR hash[32];
	if (!sha256.handle || !rsa.handle ||
	    !BCRYPT_SUCCESS(BCryptHash(sha256.handle, nullptr, 0,
	                               reinterpret_cast<PUCHAR>(const_cast<char*>(message.data())),
	                               ULONG(message.size()), hash, sizeof(hash))))
		return false;
	// BCRYPT_RSAKEY_BLOB, then the exponent and the modulus, big-endian.
	std::vector<UCHAR> blob(sizeof(BCRYPT_RSAKEY_BLOB) + exponent.size() + modulus.size());
	BCRYPT_RSAKEY_BLOB header{};
	header.Magic = BCRYPT_RSAPUBLIC_MAGIC;
	ULONG bits = ULONG(modulus.size() * 8);
	for (unsigned char top = static_cast<unsigned char>(modulus.front()); !(top & 0x80); top <<= 1)
		--bits;
	header.BitLength = bits;
	header.cbPublicExp = ULONG(exponent.size());
	header.cbModulus = ULONG(modulus.size());
	std::memcpy(blob.data(), &header, sizeof(header));
	std::memcpy(blob.data() + sizeof(header), exponent.data(), exponent.size());
	std::memcpy(blob.data() + sizeof(header) + exponent.size(), modulus.data(), modulus.size());
	BCRYPT_KEY_HANDLE key = nullptr;
	if (!BCRYPT_SUCCESS(BCryptImportKeyPair(rsa.handle, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key,
	                                        blob.data(), ULONG(blob.size()), 0)))
		return false;
	BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
	const NTSTATUS status =
		BCryptVerifySignature(key, &padding, hash, sizeof(hash),
		                      reinterpret_cast<PUCHAR>(const_cast<char*>(signature.data())),
		                      ULONG(signature.size()), BCRYPT_PAD_PKCS1);
	BCryptDestroyKey(key);
	return BCRYPT_SUCCESS(status);
}
} // namespace sn::server
