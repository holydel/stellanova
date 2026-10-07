#include "inventory_items.h"

#include <sn/sim/fitting.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sn
{
namespace
{
using namespace ph;

const sim::ShipRecord* FindShip(const sim::Account& account, u32 id)
{
	for (const sim::ShipRecord& ship : account.ships)
	{
		if (ship.id == id)
			return &ship;
	}
	return nullptr;
}

u32 Stored(const sim::Account& account, std::string_view id)
{
	for (const sim::Stack& stack : account.items)
	{
		if (stack.id == id)
			return stack.count;
	}
	return 0;
}

u32 Aboard(const sim::ShipRecord& ship, std::string_view id)
{
	for (const sim::Stack& stack : ship.fit.hold)
	{
		if (stack.id == id)
			return stack.count;
	}
	return 0;
}

// The slot a stored module goes to: `wanted` when it is of the module's
// kind, else the first empty one of its kind, else the first of its kind;
// -1 when the hull has none.
i32 SlotFor(const sim::Catalog& catalog, const sim::ShipRecord& ship, const sim::ModuleDesc& module,
            i32 wanted)
{
	const sim::HullDesc* hull = catalog.FindHull(ship.fit.hull);
	if (!hull)
		return -1;
	const std::vector<sim::SlotKind> kinds = sim::SlotsOf(*hull);
	if (wanted >= 0)
		return u32(wanted) < kinds.size() && kinds[u32(wanted)] == module.slot ? wanted : -1;
	i32 first = -1;
	for (u32 i = 0; i < kinds.size() && i < ship.fit.slots.size(); ++i)
	{
		if (kinds[i] != module.slot)
			continue;
		if (ship.fit.slots[i].id.empty())
			return i32(i);
		if (first < 0)
			first = i32(i);
	}
	return first;
}
} // namespace

std::vector<Tile> TilesOf(const sim::Catalog& catalog, const sim::Account& account, Place place)
{
	std::vector<Tile> tiles;
	switch (place.kind)
	{
		case PlaceKind::Storage:
			for (u32 r = 0; r < sim::RESOURCES; ++r)
			{
				Tile tile;
				tile.kind = TileKind::Resource;
				tile.id = sim::ResourceId(sim::Resource(r));
				tile.count = std::floor(account.resources.amount[r]);
				tiles.push_back(tile);
			}
			for (const sim::Stack& stack : account.items)
			{
				Tile tile;
				tile.kind = TileKind::Ammo;
				tile.id = stack.id;
				tile.count = stack.count;
				tiles.push_back(tile);
			}
			for (u32 i = 0; i < account.modules.size(); ++i)
			{
				Tile tile;
				tile.kind = TileKind::Module;
				tile.id = account.modules[i].id;
				tile.condition = account.modules[i].condition;
				tile.index = i32(i);
				tiles.push_back(tile);
			}
			break;
		case PlaceKind::Hold:
			if (const sim::ShipRecord* ship = FindShip(account, place.ship))
			{
				for (const sim::Stack& stack : ship->fit.hold)
				{
					Tile tile;
					tile.kind = catalog.FindAmmo(stack.id) ? TileKind::Ammo : TileKind::Loot;
					tile.id = stack.id;
					tile.count = stack.count;
					tiles.push_back(tile);
				}
			}
			break;
		case PlaceKind::Fitting:
			if (const sim::ShipRecord* ship = FindShip(account, place.ship))
			{
				const sim::HullDesc* hull = catalog.FindHull(ship->fit.hull);
				const std::vector<sim::SlotKind> kinds =
					hull ? sim::SlotsOf(*hull) : std::vector<sim::SlotKind>();
				for (u32 i = 0; i < ship->fit.slots.size(); ++i)
				{
					const sim::FittedModule& fitted = ship->fit.slots[i];
					Tile tile;
					tile.kind = fitted.id.empty() ? TileKind::EmptySlot : TileKind::Fitted;
					tile.id = fitted.id;
					tile.condition = fitted.condition;
					tile.index = i32(i);
					tile.slot = i < kinds.size() ? kinds[i] : sim::SlotKind::External;
					tiles.push_back(tile);
				}
			}
			break;
		case PlaceKind::Blueprints:
			for (const std::string& id : account.blueprints)
			{
				Tile tile;
				tile.kind = TileKind::Blueprint;
				tile.id = id;
				tiles.push_back(tile);
			}
			break;
	}
	return tiles;
}

bool Carry(const Tile& tile, Place from, Carried& carried)
{
	if (tile.kind != TileKind::Ammo && tile.kind != TileKind::Module &&
	    tile.kind != TileKind::Fitted)
		return false;
	if (tile.id.size() >= sizeof(carried.id))
		return false;
	carried = {};
	carried.kind = tile.kind;
	carried.from = from;
	carried.index = tile.index;
	carried.count = u32(tile.count);
	std::memcpy(carried.id, tile.id.data(), tile.id.size());
	return true;
}

bool DropOn(const sim::Catalog& catalog, const sim::Account& account, const Carried& carried,
            Place onto, i32 slot, sim::Operation& operation, std::string& why)
{
	why.clear();
	operation = {};
	const std::string id = carried.id;
	// A ship's node, its hold and its fitting all mean the ship.
	const bool ontoShip = onto.kind == PlaceKind::Hold || onto.kind == PlaceKind::Fitting;
	if (onto == carried.from && slot < 0)
		return false;
	switch (carried.kind)
	{
		case TileKind::Ammo:
			if (carried.from.kind == PlaceKind::Storage && ontoShip)
			{
				// As many as storage has and the hold takes.
				const sim::ShipRecord* ship = FindShip(account, onto.ship);
				const sim::HullDesc* hull = ship ? catalog.FindHull(ship->fit.hull) : nullptr;
				f32 volume = 0.0f;
				f32 mass = 0.0f;
				f32 each = 0.0f;
				if (!hull || !sim::HoldSize(catalog, ship->fit.hold, volume, mass) ||
				    !catalog.ItemSize(id, each, mass))
				{
					why = "error.item";
					return false;
				}
				const f32 room = std::max(0.0f, hull->cargo - volume);
				const u32 fits =
					each > 0.0f ? u32(std::floor(room / each + 1e-4f)) : Stored(account, id);
				const u32 count = std::min(Stored(account, id), fits);
				if (count == 0)
				{
					why = "error.hold";
					return false;
				}
				operation.kind = sim::Operation::Kind::Load;
				operation.ship = onto.ship;
				operation.id = id;
				operation.count = i32(count);
				return true;
			}
			if (carried.from.kind == PlaceKind::Hold && onto.kind == PlaceKind::Storage)
			{
				const sim::ShipRecord* ship = FindShip(account, carried.from.ship);
				const u32 count = ship ? Aboard(*ship, id) : 0;
				if (count == 0)
				{
					why = "error.item";
					return false;
				}
				operation.kind = sim::Operation::Kind::Load;
				operation.ship = carried.from.ship;
				operation.id = id;
				operation.count = -i32(count);
				return true;
			}
			if (carried.from.kind == PlaceKind::Hold && ontoShip && onto.ship == carried.from.ship)
				return false;
			why = "inventory.no_move";
			return false;
		case TileKind::Module:
		{
			if (!ontoShip)
			{
				why = onto.kind == PlaceKind::Storage ? "" : "inventory.no_move";
				return false;
			}
			const sim::ShipRecord* ship = FindShip(account, onto.ship);
			const sim::ModuleDesc* module = catalog.FindModule(id);
			if (!ship || !module || carried.index < 0 ||
			    u32(carried.index) >= account.modules.size() ||
			    account.modules[u32(carried.index)].id != id)
			{
				why = "error.item";
				return false;
			}
			const i32 into = SlotFor(catalog, *ship, *module, slot);
			if (into < 0)
			{
				why = "error.slot";
				return false;
			}
			operation.kind = sim::Operation::Kind::Fit;
			operation.ship = onto.ship;
			operation.slot = u32(into);
			operation.module = carried.index;
			return true;
		}
		case TileKind::Fitted:
			if (onto.kind == PlaceKind::Storage)
			{
				operation.kind = sim::Operation::Kind::Fit;
				operation.ship = carried.from.ship;
				operation.slot = u32(carried.index);
				operation.module = -1;
				return true;
			}
			if (ontoShip && onto.ship == carried.from.ship)
				return false;
			why = "inventory.no_move";
			return false;
		case TileKind::Resource: why = "inventory.resources_stay"; return false;
		case TileKind::EmptySlot:
		case TileKind::Loot:
		case TileKind::Blueprint: why = "inventory.no_move"; return false;
	}
	return false;
}

void SeedInventoryTest(const sim::Catalog& catalog, sim::Account& account)
{
	// Built as a player would, paid from plenty.
	account.resources[sim::Resource::Metal] = 1e6;
	account.resources[sim::Resource::He3] = 1e6;
	account.resources[sim::Resource::Chips] = 1e6;
	const i64 now = account.updated;
	std::string why;
	const auto build = [&](const char* id, u32 times)
	{
		for (u32 i = 0; i < times; ++i)
			sim::Build(catalog, account, id, now, why);
	};
	build("lancer", 2);
	build("laser_s", 10);
	build("plasma_s", 10);
	build("shield_booster_s", 4);
	build("capacitor_battery_s", 4);
	build("plasma_charge_s", 30);
	// Some worn, to show their bars.
	for (u32 i = 0; i < account.modules.size(); i += 3)
		account.modules[i].condition = 0.35f + 0.1f * f32(i % 5);
	account.resources[sim::Resource::Metal] = 4200.0;
	account.resources[sim::Resource::He3] = 1900.0;
	account.resources[sim::Resource::Chips] = 760.0;
}
} // namespace sn
