#pragma once

#include <sn/sim/fitting.h>
#include <sn/sim/starmap.h>

#include <string>
#include <string_view>
#include <vector>

// The solo loop's account (docs/solo-loop.md, docs/adr/0014-accounts-on-the-game-server.md,
// 0016-sign-in-and-admin.md):
// a colony that produces from the clock, storage, ships, blueprints and the
// star map's progress. Plain data and functions: the server applies them on
// its clock; the client uses them for previews. Times are seconds since
// 1970. Operations answer why not with a strings-table key ("error.metal").
namespace sn::sim
{
using ph::i64;

struct StoredModule
{
	std::string id;
	f32 condition = 1.0f;
};

struct ShipRecord
{
	u32 id = 0;
	ShipFit fit;
	bool away = false; // in a battle
};

struct AccountStats
{
	u32 battles = 0;
	u32 won = 0;
	u32 lost = 0; // ships
	u32 kills = 0;
};

// A sign-in an account has: a provider's id for the player.
struct Identity
{
	std::string provider; // "steam", "google", "apple", "discord"
	std::string id;
	std::string name; // the name it gave, if any (the admin's to see)
};

struct Account
{
	std::string key; // the key it was made with: kept to compare (never sent back)
	// More keys that open it, handed out at sign-in (never sent back).
	std::vector<std::string> keys;
	std::vector<Identity> identities; // none: a guest, off the leaderboard
	bool banned = false;              // it cannot log in
	bool hidden = false;              // off the leaderboard
	// A guest the admin keeps: saved without a sign-in, its device's key
	// opening it, until a sign-in joins it (the developer's phone before its
	// store's sign-in works: ADR 0016's addendum of 2026-10-06).
	bool kept = false;
	std::string name;
	i64 created = 0;
	i64 updated = 0; // production is counted up to here
	Resources resources;
	f64 command = 0.0; // command points
	u32 levels[BUILDINGS] = {1, 1, 1, 1, 1};
	// When each building's upgrade is done; 0 while it has none. Buildings
	// upgrade each on its own, at the same time.
	i64 upgradeDone[BUILDINGS] = {};
	std::vector<Stack> items;          // ammo in storage
	std::vector<StoredModule> modules; // modules in storage
	std::vector<std::string> blueprints;
	std::vector<ShipRecord> ships;
	u32 nextShip = 1;
	std::vector<Hex> cleared;
	u32 deepest = 0; // the deepest ring cleared: the leaderboard's
	i64 deepestAt = 0;
	AccountStats stats;

	const ShipRecord* FindShip(u32 id) const;
	ShipRecord* FindShip(u32 id);
	bool IsGuest() const { return identities.empty(); }
	bool IsUpgrading(Building building) const { return upgradeDone[u32(building)] != 0; }
	// The first sign-in's provider; "" for a guest.
	std::string_view Provider() const
	{
		return identities.empty() ? std::string_view() : identities.front().provider;
	}
	bool Opens(std::string_view candidate) const;
	const Identity* FindIdentity(std::string_view provider) const;
};

constexpr u32 MAX_SHIPS_KEPT = 12;
constexpr u32 MAX_MODULES_KEPT = 60;
constexpr u32 MAX_KEYS_KEPT = 8; // devices signed in: the oldest key goes first

// A new account, as the catalog starts it.
Account NewAccount(const Catalog& catalog, std::string key, std::string name, i64 now);

// The colony's numbers at a level.
f64 ProductionPerHour(const Catalog& catalog, Building building, u32 level);
f64 StorageOf(const Catalog& catalog, u32 depotLevel);
f64 CommandCap(const Catalog& catalog, u32 commandLevel);
f64 CommandRefill(const Catalog& catalog, u32 commandLevel); // s a point
// From `level` to the next.
Resources UpgradeCost(const Catalog& catalog, Building building, u32 level);
f64 UpgradeSeconds(const Catalog& catalog, u32 level);

// Production and command points counted up to `now`, and the upgrades that
// are done finished, each at its time (the old rates until then, the new
// ones after).
void Advance(const Catalog& catalog, Account& account, i64 now);

// Each advances the account to `now` first.
bool Upgrade(const Catalog& catalog, Account& account, Building building, i64 now,
             std::string& why);
// A ship (an empty hull), a module, or a batch of ammo, from a known blueprint.
bool Build(const Catalog& catalog, Account& account, std::string_view blueprint, i64 now,
           std::string& why);
// Storage's module `stored` into a ship's slot (what was there goes to
// storage); `stored` -1 empties the slot.
bool FitModule(const Catalog& catalog, Account& account, u32 ship, u32 slot, i32 stored,
               std::string& why);
// Ammo from storage into a ship's hold (`count` > 0), or back (< 0).
bool LoadHold(const Catalog& catalog, Account& account, u32 ship, std::string_view item, i32 count,
              std::string& why);
f64 RepairCost(const Catalog& catalog, const ShipRecord& ship); // metal
bool Repair(const Catalog& catalog, Account& account, u32 ship, i64 now, std::string& why);

// The star map, as the account sees it.
bool IsCleared(const Account& account, Hex hex);
// Next to the colony or to a cleared node.
bool CanAttack(const Account& account, Hex hex);
// Within the fog's reach of the colony or a cleared node.
bool IsVisible(const Catalog& catalog, const Account& account, Hex hex);

struct LaunchCost
{
	f64 command = 0.0;
	f64 fuel = 0.0; // He-3
};
LaunchCost CostOfLaunch(const Catalog& catalog, Hex hex, u32 shipCount);
// Checks a launch and pays for it; the ships are away until the outcome.
bool Launch(const Catalog& catalog, Account& account, Hex hex, const std::vector<u32>& ships,
            i64 now, std::string& why);

// What a crate holds, rolled by the server.
struct Loot
{
	Resources resources;
	std::vector<Stack> items; // ammo and modules
};
Loot RollLoot(const Catalog& catalog, const Node& node, u32& random);
void LootSize(const Catalog& catalog, const Loot& loot, f32& volume, f32& mass);
// Adds loot (or a hold) into a list of stacks: resources as their ids.
void AddLoot(std::vector<Stack>& into, const Loot& loot);

// How a battle ended, from the server.
struct ShipOutcome
{
	u32 id = 0;
	bool lost = false;
	f32 condition = 1.0f;
	std::vector<f32> modules; // each slot's condition
	std::vector<Stack> hold;  // what it brought back, loot included
};

struct BattleOutcome
{
	Hex node;
	bool cleared = false;
	u32 kills = 0;
	std::vector<ShipOutcome> ships;
};

// Survivors home: resources in their holds go to the colony, modules to
// storage, ammo stays aboard; lost ships are gone. A cleared node is
// explored, and the deepest ring kept. Then, with no ship and too little
// metal for one, the rescue ship.
void ApplyOutcome(const Catalog& catalog, Account& account, const BattleOutcome& outcome, i64 now);
// Ships away when no battle can be running (a server that restarted) come
// home as they left.
void BringHome(Account& account);

// JSON, as the server keeps it (with the keys) and the client gets it
// (without). False when the text is not an account. Version 1 (before
// sign-ins; He-3 was "he4") reads too.
std::string ToJson(const Account& account, bool withKey);
bool FromJson(std::string_view json, Account& account);

// The account as Profile carries it: without the key, with the server's
// clock (the client counts production on from there).
std::string ProfileJson(const Account& account, i64 now);
bool ReadProfile(std::string_view json, Account& account, i64& now);

// A client's request on its account (protocol's Request).
struct Operation
{
	enum class Kind : u8
	{
		Refresh, // only a new Profile
		Upgrade, // `building`
		Build,   // blueprint `id`
		Fit,     // storage's module `module` (-1: none) into `ship`'s `slot`
		Load,    // `count` of item `id` into `ship`'s hold (negative: out)
		Repair,  // `ship`
		Launch,  // `ships` to `node`
		Return,  // the battle's fleet home
	};
	Kind kind = Kind::Refresh;
	Building building = Building::Mine;
	std::string id;
	u32 ship = 0;
	u32 slot = 0;
	i32 module = -1;
	i32 count = 0;
	Hex node;
	std::vector<u32> ships;
};
std::string ToJson(const Operation& operation);
bool FromJson(std::string_view json, Operation& operation);

// How a battle ended, as the client is told (protocol's Result).
struct BattleReport
{
	enum class End : u8
	{
		Cleared,  // every enemy destroyed
		Returned, // the fleet came home before
		Lost,     // every ship destroyed
	};
	End end = End::Returned;
	Hex node;
	u32 kills = 0;
	u32 lost = 0;            // ships
	std::vector<Stack> loot; // brought home
};
std::string ToJson(const BattleReport& report);
bool FromJson(std::string_view json, BattleReport& report);
} // namespace sn::sim
