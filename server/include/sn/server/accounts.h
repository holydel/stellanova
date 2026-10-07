#pragma once

#include <sn/sim/account.h>
#include <sn/sim/protocol.h>

#include <map>
#include <string>
#include <string_view>
#include <vector>

// Where the server keeps accounts (docs/adr/0014-accounts-on-the-game-server.md,
// 0016-sign-in-and-admin.md): JSON files until a backend vendor replaces the
// store, and the leaderboard made from them. Links find an account by a
// sign-in or by a key handed out at sign-in.
namespace sn::server
{
using ph::i64;
using ph::u32;

class AccountStore
{
public:
	virtual ~AccountStore() = default;
	// An account's JSON by its id; false when there is none.
	virtual bool Load(std::string_view id, std::string& json) = 0;
	// Whole or not at all.
	virtual bool Save(std::string_view id, std::string_view json) = 0;
	// Every account's id.
	virtual std::vector<std::string> List() = 0;
	// Gone from the list; false when there was none.
	virtual bool Remove(std::string_view id) = 0;
	// A link's account id by its name (LinkName); false when there is none.
	virtual bool LoadLink(std::string_view name, std::string& id) = 0;
	virtual bool SaveLink(std::string_view name, std::string_view id) = 0;
	virtual void RemoveLink(std::string_view name) = 0;
};

// Files in a folder, `<id>.json` each, written to a new file and renamed
// over the old one. Links are `links/<name>.txt`; a removed account moves
// to `deleted/<id>-<time>.json`.
class FileStore final : public AccountStore
{
public:
	// The folder is made if it is missing.
	explicit FileStore(std::string folder);
	bool Load(std::string_view id, std::string& json) override;
	bool Save(std::string_view id, std::string_view json) override;
	std::vector<std::string> List() override;
	bool Remove(std::string_view id) override;
	bool LoadLink(std::string_view name, std::string& id) override;
	bool SaveLink(std::string_view name, std::string_view id) override;
	void RemoveLink(std::string_view name) override;

private:
	std::string PathOf(std::string_view id) const;
	std::string LinkPath(std::string_view name) const;

	std::string folder;
};

// In memory: tests, and local games without a folder.
class MemoryStore final : public AccountStore
{
public:
	bool Load(std::string_view id, std::string& json) override;
	bool Save(std::string_view id, std::string_view json) override;
	std::vector<std::string> List() override;
	bool Remove(std::string_view id) override;
	bool LoadLink(std::string_view name, std::string& id) override;
	bool SaveLink(std::string_view name, std::string_view id) override;
	void RemoveLink(std::string_view name) override;

private:
	std::map<std::string, std::string, std::less<>> accounts;
	std::map<std::string, std::string, std::less<>> links;
};

// An account's id from its key: a hash, as hex. Keys are hex digits too.
std::string AccountId(std::string_view key);
bool IsKey(std::string_view key);
// A link's name: for a sign-in ("steam-<hash of the id>"), or for a key
// handed out at sign-in ("key-<hash>").
std::string LinkName(std::string_view provider, std::string_view id);
std::string KeyLinkName(std::string_view key);

// The deepest rings cleared, best first; ties go to whoever got there first.
class Leaderboard
{
public:
	struct Entry
	{
		std::string id;
		std::string name;
		u32 ring = 0;
		i64 at = 0;
		std::string provider;
	};

	// True when the order or a shown name changed. Ring 0 takes the entry
	// off.
	bool Update(std::string_view id, std::string_view name, u32 ring, i64 at,
	            std::string_view provider = {});
	const std::vector<Entry>& Entries() const { return entries; }
	// {"season", "updated", "entries": [{"rank", "name", "ring", "at", "provider"}]}:
	// the first `count`, without ids.
	std::string Json(u32 season, i64 now, u32 count) const;

private:
	std::vector<Entry> entries;
};

// Writes text to a file whole: a new file, then a rename.
bool WriteWhole(const std::string& path, std::string_view text);
} // namespace sn::server
