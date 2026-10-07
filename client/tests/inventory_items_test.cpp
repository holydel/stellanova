#include "inventory_items.h"

#include <sn/sim/catalog.h>

#include <doctest/doctest.h>

#include <algorithm>

using namespace sn;
using namespace ph;

namespace
{
sim::Account Seeded()
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::Account account = sim::NewAccount(catalog, "key", "Ann", 1000);
	SeedInventoryTest(catalog, account);
	return account;
}

const Tile* Find(const std::vector<Tile>& tiles, TileKind kind, std::string_view id)
{
	for (const Tile& tile : tiles)
	{
		if (tile.kind == kind && tile.id == id)
			return &tile;
	}
	return nullptr;
}
} // namespace

TEST_CASE("inventory: the test account has many separate things")
{
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::Account account = Seeded();
	CHECK(account.ships.size() == 3);
	const std::vector<Tile> tiles = TilesOf(catalog, account, {PlaceKind::Storage, 0});
	// Resources first, each a tile with its amount.
	REQUIRE(tiles.size() > 3);
	CHECK(tiles[0].kind == TileKind::Resource);
	CHECK(tiles[0].id == "metal");
	CHECK(tiles[0].count == 4200.0);
	// Ten turrets of each kind, each its own tile, some worn.
	const auto count = [&](std::string_view id)
	{
		return std::count_if(tiles.begin(), tiles.end(), [&](const Tile& tile)
		                     { return tile.kind == TileKind::Module && tile.id == id; });
	};
	CHECK(count("laser_s") >= 10);
	CHECK(count("plasma_s") >= 10);
	CHECK(std::any_of(tiles.begin(), tiles.end(),
	                  [](const Tile& tile) { return tile.condition < 1.0f; }));
	const Tile* charges = Find(tiles, TileKind::Ammo, "plasma_charge_s");
	REQUIRE(charges);
	CHECK(charges->count >= 3000.0);
}

TEST_CASE("inventory: drops become the server's requests")
{
	const sim::Catalog& catalog = sim::GetCatalog();
	sim::Account account = Seeded();
	const u32 ship = account.ships.back().id; // a new, empty Lancer
	const Place storage = {PlaceKind::Storage, 0};
	const Place hold = {PlaceKind::Hold, ship};
	const Place fitting = {PlaceKind::Fitting, ship};
	const std::vector<Tile> tiles = TilesOf(catalog, account, storage);
	sim::Operation operation;
	std::string why;
	Carried carried;

	SUBCASE("charges onto a ship: as many as its hold takes")
	{
		REQUIRE(Carry(*Find(tiles, TileKind::Ammo, "plasma_charge_s"), storage, carried));
		REQUIRE(DropOn(catalog, account, carried, hold, -1, operation, why));
		CHECK(operation.kind == sim::Operation::Kind::Load);
		CHECK(operation.ship == ship);
		CHECK(operation.id == "plasma_charge_s");
		const sim::HullDesc* hull = catalog.FindHull("lancer");
		f32 each = 0.0f;
		f32 mass = 0.0f;
		REQUIRE(catalog.ItemSize("plasma_charge_s", each, mass));
		CHECK(operation.count > 0);
		CHECK(f32(operation.count) * each <= hull->cargo + 1e-3f);
		// The sim agrees: it loads them.
		REQUIRE(sim::LoadHold(catalog, account, ship, operation.id, operation.count, why));
		// And back home, all of them.
		const std::vector<Tile> aboard = TilesOf(catalog, account, hold);
		REQUIRE(aboard.size() == 1);
		REQUIRE(Carry(aboard[0], hold, carried));
		REQUIRE(DropOn(catalog, account, carried, storage, -1, operation, why));
		CHECK(operation.count == -i32(aboard[0].count));
	}
	SUBCASE("a module onto a ship: into a free slot of its kind")
	{
		const Tile* laser = Find(tiles, TileKind::Module, "laser_s");
		REQUIRE(laser);
		REQUIRE(Carry(*laser, storage, carried));
		REQUIRE(DropOn(catalog, account, carried, fitting, -1, operation, why));
		CHECK(operation.kind == sim::Operation::Kind::Fit);
		CHECK(operation.module == laser->index);
		REQUIRE(sim::FitModule(catalog, account, ship, operation.slot, operation.module, why));
		// Fitted, it goes back to storage when dropped there.
		const std::vector<Tile> slots = TilesOf(catalog, account, fitting);
		const Tile* fitted = Find(slots, TileKind::Fitted, "laser_s");
		REQUIRE(fitted);
		REQUIRE(Carry(*fitted, fitting, carried));
		REQUIRE(DropOn(catalog, account, carried, storage, -1, operation, why));
		CHECK(operation.module == -1);
		CHECK(operation.slot == u32(fitted->index));
	}
	SUBCASE("a module onto a slot of another kind is refused")
	{
		const Tile* booster = Find(tiles, TileKind::Module, "shield_booster_s");
		REQUIRE(booster);
		REQUIRE(Carry(*booster, storage, carried));
		const std::vector<Tile> slots = TilesOf(catalog, account, fitting);
		const auto external = std::find_if(slots.begin(), slots.end(), [](const Tile& tile)
		                                   { return tile.slot == sim::SlotKind::External; });
		REQUIRE(external != slots.end());
		CHECK(!DropOn(catalog, account, carried, fitting, external->index, operation, why));
		CHECK(why == "error.slot");
	}
	SUBCASE("resources stay; onto its own place, nothing")
	{
		CHECK(!Carry(tiles[0], storage, carried));
		REQUIRE(Carry(*Find(tiles, TileKind::Ammo, "plasma_charge_s"), storage, carried));
		CHECK(!DropOn(catalog, account, carried, storage, -1, operation, why));
		CHECK(why.empty());
	}
}
