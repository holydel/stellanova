#pragma once

#include <sn/sim/account.h>

#include <string>
#include <vector>

// The inventory's items (docs/adr/0017-inventory.md): the account's places,
// the tiles each shows, and what an item dragged onto a place asks the
// server. Plain data, for the window (inventory.h) and the tests.
namespace sn
{
// Where things are: the colony's storage, a ship's hold or its fitting, or
// the blueprints.
enum class PlaceKind : ph::u8
{
	Storage,
	Hold,
	Fitting,
	Blueprints,
};

struct Place
{
	PlaceKind kind = PlaceKind::Storage;
	ph::u32 ship = 0; // the ship's id, for its hold and its fitting

	bool operator==(const Place&) const = default;
};

enum class TileKind : ph::u8
{
	Resource,  // metal, He-3, chips in storage
	Ammo,      // a stack of charges, in storage or a hold
	Module,    // a module in storage, each its own
	Fitted,    // a module in a slot
	EmptySlot, // a slot without one
	Loot,      // anything else in a hold (from a battle; home it goes to storage)
	Blueprint, // something the colony can build
};

// One tile of a place, as the grid shows it.
struct Tile
{
	TileKind kind = TileKind::Resource;
	std::string id;           // the catalog's ("" for an empty slot)
	ph::f64 count = 1.0;      // a stack's or a resource's amount
	ph::f32 condition = 1.0f; // a module's
	// A stored module's index in Account::modules; a slot's number.
	ph::i32 index = -1;
	sim::SlotKind slot = sim::SlotKind::External; // fitted and empty slots'
};

// A place's tiles: storage's resources, then its ammo, then each module;
// a hold's stacks; a fitting's slots in order; the blueprints.
std::vector<Tile> TilesOf(const sim::Catalog& catalog, const sim::Account& account, Place place);

// What is dragged: plain bytes, as ph::ui's drags copy them.
struct Carried
{
	TileKind kind = TileKind::Resource;
	Place from;
	ph::i32 index = -1;
	ph::u32 count = 0;
	char id[48] = {};
};
// The tile's drag; false for what does not move (resources, empty slots,
// loot, blueprints).
bool Carry(const Tile& tile, Place from, Carried& carried);

// The server's request for `carried` dropped onto `onto` (a slot's number
// when dropped on one of a fitting's tiles, else -1): false with why (a
// strings key; "" when there is nothing to do, as onto its own place).
bool DropOn(const sim::Catalog& catalog, const sim::Account& account, const Carried& carried,
            Place onto, ph::i32 slot, sim::Operation& operation, std::string& why);

// Many things to drag: the inventory test's account (--inventory-test).
void SeedInventoryTest(const sim::Catalog& catalog, sim::Account& account);
} // namespace sn
