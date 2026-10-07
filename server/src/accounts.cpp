#include <sn/server/accounts.h>

#include <ph/core/log.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace sn::server
{
namespace
{
namespace fs = std::filesystem;
using ph::u8;

constexpr std::string_view EXTENSION = ".json";

bool ReadWhole(const std::string& path, std::string& text)
{
	FILE* file = std::fopen(path.c_str(), "rb");
	if (!file)
		return false;
	text.clear();
	char buffer[4096];
	ph::usize count = 0;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		text.append(buffer, count);
	std::fclose(file);
	return true;
}

// A JSON string's characters, escaped.
std::string Quote(std::string_view text)
{
	std::string out = "\"";
	for (const char c : text)
	{
		if (c == '"' || c == '\\')
			out += '\\';
		if (u32(u8(c)) < 0x20)
		{
			char escaped[8];
			std::snprintf(escaped, sizeof(escaped), "\\u%04x", u32(u8(c)));
			out += escaped;
			continue;
		}
		out += c;
	}
	return out + "\"";
}
} // namespace

bool WriteWhole(const std::string& path, std::string_view text)
{
	const std::string fresh = path + ".new";
	FILE* file = std::fopen(fresh.c_str(), "wb");
	if (!file)
		return false;
	const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
	const bool closed = std::fclose(file) == 0;
	if (!written || !closed)
	{
		std::remove(fresh.c_str());
		return false;
	}
	std::error_code error;
	fs::rename(fresh, path, error);
	return !error;
}

FileStore::FileStore(std::string where) : folder(std::move(where))
{
	std::error_code error;
	fs::create_directories(folder, error);
	if (error)
		PH_LOG_ERROR("accounts: cannot make %s: %s", folder.c_str(), error.message().c_str());
}

std::string FileStore::PathOf(std::string_view id) const
{
	return (fs::path(folder) / (std::string(id) + std::string(EXTENSION))).string();
}

bool FileStore::Load(std::string_view id, std::string& json) { return ReadWhole(PathOf(id), json); }

bool FileStore::Save(std::string_view id, std::string_view json)
{
	if (WriteWhole(PathOf(id), json))
		return true;
	PH_LOG_ERROR("accounts: cannot save %.*s", int(id.size()), id.data());
	return false;
}

std::string FileStore::LinkPath(std::string_view name) const
{
	return (fs::path(folder) / "links" / (std::string(name) + ".txt")).string();
}

bool FileStore::Remove(std::string_view id)
{
	std::error_code error;
	const fs::path from = PathOf(id);
	if (!fs::exists(from, error))
		return false;
	const fs::path bin = fs::path(folder) / "deleted";
	fs::create_directories(bin, error);
	fs::rename(from, bin / (std::string(id) + "-" + std::to_string(std::time(nullptr)) + ".json"),
	           error);
	if (error)
		PH_LOG_ERROR("accounts: cannot remove %.*s: %s", int(id.size()), id.data(),
		             error.message().c_str());
	return !error;
}

bool FileStore::LoadLink(std::string_view name, std::string& id)
{
	if (!ReadWhole(LinkPath(name), id))
		return false;
	while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
		id.pop_back();
	return !id.empty();
}

bool FileStore::SaveLink(std::string_view name, std::string_view id)
{
	std::error_code error;
	fs::create_directories(fs::path(folder) / "links", error);
	if (WriteWhole(LinkPath(name), id))
		return true;
	PH_LOG_ERROR("accounts: cannot save link %.*s", int(name.size()), name.data());
	return false;
}

void FileStore::RemoveLink(std::string_view name)
{
	std::error_code error;
	fs::remove(LinkPath(name), error);
}

std::vector<std::string> FileStore::List()
{
	std::vector<std::string> ids;
	std::error_code error;
	for (fs::directory_iterator entry(folder, error), end; !error && entry != end;
	     entry.increment(error))
	{
		const fs::path& path = entry->path();
		if (path.extension() == EXTENSION)
			ids.push_back(path.stem().string());
	}
	return ids;
}

bool MemoryStore::Load(std::string_view id, std::string& json)
{
	const auto found = accounts.find(id);
	if (found == accounts.end())
		return false;
	json = found->second;
	return true;
}

bool MemoryStore::Save(std::string_view id, std::string_view json)
{
	accounts[std::string(id)] = std::string(json);
	return true;
}

std::vector<std::string> MemoryStore::List()
{
	std::vector<std::string> ids;
	for (const auto& [id, json] : accounts)
		ids.push_back(id);
	return ids;
}

bool MemoryStore::Remove(std::string_view id)
{
	const auto found = accounts.find(id);
	if (found == accounts.end())
		return false;
	accounts.erase(found);
	return true;
}

bool MemoryStore::LoadLink(std::string_view name, std::string& id)
{
	const auto found = links.find(name);
	if (found == links.end())
		return false;
	id = found->second;
	return true;
}

bool MemoryStore::SaveLink(std::string_view name, std::string_view id)
{
	links[std::string(name)] = std::string(id);
	return true;
}

void MemoryStore::RemoveLink(std::string_view name)
{
	const auto found = links.find(name);
	if (found != links.end())
		links.erase(found);
}

bool IsKey(std::string_view key)
{
	return key.size() == sim::KEY_BYTES &&
	       std::all_of(key.begin(), key.end(),
	                   [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string AccountId(std::string_view key)
{
	ph::u64 hash = 14695981039346656037ull; // FNV-1a
	for (const char c : key)
		hash = (hash ^ u8(c)) * 1099511628211ull;
	char id[17];
	std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(hash));
	return id;
}

std::string LinkName(std::string_view provider, std::string_view id)
{
	return std::string(provider) + "-" + AccountId(std::string(provider) + ":" + std::string(id));
}

std::string KeyLinkName(std::string_view key) { return "key-" + AccountId(key); }

bool Leaderboard::Update(std::string_view id, std::string_view name, u32 ring, i64 at,
                         std::string_view provider)
{
	const auto found =
		std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == id; });
	if (found != entries.end() && found->ring == ring && found->name == name &&
	    found->provider == provider)
		return false;
	if (ring == 0 && found == entries.end())
		return false;
	if (found != entries.end())
		entries.erase(found);
	if (ring > 0)
		entries.push_back({std::string(id), std::string(name), ring, at, std::string(provider)});
	std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b)
	                 { return a.ring != b.ring ? a.ring > b.ring : a.at < b.at; });
	return true;
}

std::string Leaderboard::Json(u32 season, i64 now, u32 count) const
{
	std::string json = "{\"season\":" + std::to_string(season) +
	                   ",\"updated\":" + std::to_string(now) + ",\"entries\":[";
	for (u32 i = 0; i < std::min(count, u32(entries.size())); ++i)
	{
		const Entry& entry = entries[i];
		json += (i ? "," : "") + std::string("{\"rank\":") + std::to_string(i + 1) +
		        ",\"name\":" + Quote(entry.name) + ",\"ring\":" + std::to_string(entry.ring) +
		        ",\"at\":" + std::to_string(entry.at) + ",\"provider\":" + Quote(entry.provider) +
		        "}";
	}
	return json + "]}";
}
} // namespace sn::server
