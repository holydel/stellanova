#include "json.h"

#include <sn/sim/account.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace sn::sim
{
namespace
{
constexpr u32 ACCOUNT_VERSION = 2;

f32 Next(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return f32(state >> 8) / f32(1u << 24);
}

u32 CountOf(const std::vector<Stack>& stacks, std::string_view id)
{
	for (const Stack& stack : stacks)
	{
		if (stack.id == id)
			return stack.count;
	}
	return 0;
}

void Add(std::vector<Stack>& stacks, std::string_view id, i64 count)
{
	for (auto stack = stacks.begin(); stack != stacks.end(); ++stack)
	{
		if (stack->id != id)
			continue;
		const i64 total = std::max<i64>(0, i64(stack->count) + count);
		if (total == 0)
			stacks.erase(stack);
		else
			stack->count = u32(std::min<i64>(total, 0xffffffffll));
		return;
	}
	if (count > 0)
		stacks.push_back({std::string(id), u32(std::min<i64>(count, 0xffffffffll))});
}

bool CanPay(const Account& account, const Resources& cost)
{
	for (u32 r = 0; r < RESOURCES; ++r)
	{
		if (account.resources.amount[r] + 1e-6 < cost.amount[r])
			return false;
	}
	return true;
}

// The key of the first resource the account lacks.
const char* Lacking(const Account& account, const Resources& cost)
{
	static constexpr const char* KEYS[RESOURCES] = {"error.metal", "error.he3", "error.chips"};
	for (u32 r = 0; r < RESOURCES; ++r)
	{
		if (account.resources.amount[r] + 1e-6 < cost.amount[r])
			return KEYS[r];
	}
	return "";
}

void Pay(Account& account, const Resources& cost)
{
	for (u32 r = 0; r < RESOURCES; ++r)
		account.resources.amount[r] = std::max(0.0, account.resources.amount[r] - cost.amount[r]);
}

bool Knows(const Account& account, std::string_view blueprint)
{
	return std::find(account.blueprints.begin(), account.blueprints.end(), blueprint) !=
	       account.blueprints.end();
}

// Production and command points over `seconds` at the account's levels.
void Produce(const Catalog& catalog, Account& account, f64 seconds)
{
	if (seconds <= 0.0)
		return;
	const f64 cap = StorageOf(catalog, account.levels[u32(Building::Depot)]);
	for (const Building building : {Building::Mine, Building::Extractor, Building::Fab})
	{
		const BuildingDesc& desc = catalog.colony.buildings[u32(building)];
		f64& amount = account.resources[desc.produces];
		if (amount < cap)
			amount = std::min(
				cap, amount + ProductionPerHour(catalog, building, account.levels[u32(building)]) *
								  seconds / 3600.0);
	}
	const u32 level = account.levels[u32(Building::Command)];
	const f64 points = CommandCap(catalog, level);
	if (account.command < points)
		account.command =
			std::min(points, account.command + seconds / CommandRefill(catalog, level));
}

ShipRecord MakeRecord(const Catalog& catalog, Account& account, const ShipPlan& plan)
{
	ShipRecord ship;
	ship.id = account.nextShip++;
	ship.fit = FitFromPlan(catalog, plan);
	return ship;
}

// With no ship and too little metal for one, the colony gives one.
void Rescue(const Catalog& catalog, Account& account)
{
	if (!account.ships.empty())
		return;
	const HullDesc* hull = catalog.FindHull(catalog.colony.rescue.hull);
	if (hull && CanPay(account, hull->cost) && Knows(account, hull->id))
		return;
	account.ships.push_back(MakeRecord(catalog, account, catalog.colony.rescue));
}

Resources Scaled(const Resources& base, f64 times)
{
	Resources scaled;
	for (u32 r = 0; r < RESOURCES; ++r)
		scaled.amount[r] = base.amount[r] * times;
	return scaled;
}

// --- JSON ---

using json::Builder;

yyjson_mut_val* WriteStacks(const Builder& out, const std::vector<Stack>& stacks)
{
	yyjson_mut_val* object = out.Object();
	for (const Stack& stack : stacks)
		out.SetNumberAt(object, stack.id, f64(stack.count));
	return object;
}

std::vector<Stack> ReadStacks(yyjson_val* object)
{
	std::vector<Stack> stacks;
	usize index = 0;
	usize max = 0;
	yyjson_val* key = nullptr;
	yyjson_val* value = nullptr;
	if (yyjson_is_obj(object))
		yyjson_obj_foreach(object, index, max, key, value)
		{
			if (yyjson_is_num(value) && yyjson_get_num(value) >= 1.0)
				stacks.push_back({std::string(json::Text(key)), u32(yyjson_get_num(value))});
		}
	return stacks;
}

i64 Integer(yyjson_val* object, const char* key) { return i64(json::Number(object, key, 0.0)); }
} // namespace

const ShipRecord* Account::FindShip(u32 id) const
{
	for (const ShipRecord& ship : ships)
	{
		if (ship.id == id)
			return &ship;
	}
	return nullptr;
}

ShipRecord* Account::FindShip(u32 id)
{
	return const_cast<ShipRecord*>(static_cast<const Account*>(this)->FindShip(id));
}

bool Account::Opens(std::string_view candidate) const
{
	if (candidate.empty())
		return false;
	if (candidate == key)
		return true;
	return std::find(keys.begin(), keys.end(), candidate) != keys.end();
}

const Identity* Account::FindIdentity(std::string_view provider) const
{
	for (const Identity& identity : identities)
	{
		if (identity.provider == provider)
			return &identity;
	}
	return nullptr;
}

Account NewAccount(const Catalog& catalog, std::string key, std::string name, i64 now)
{
	Account account;
	account.key = std::move(key);
	account.name = std::move(name);
	account.created = now;
	account.updated = now;
	account.resources = catalog.colony.startResources;
	account.command = CommandCap(catalog, 1);
	for (const ShipPlan& plan : catalog.colony.startShips)
		account.ships.push_back(MakeRecord(catalog, account, plan));
	for (const std::string& id : catalog.colony.startModules)
		account.modules.push_back({id, 1.0f});
	account.items = catalog.colony.startItems;
	account.blueprints = catalog.colony.blueprints;
	return account;
}

f64 ProductionPerHour(const Catalog& catalog, Building building, u32 level)
{
	const BuildingDesc& desc = catalog.colony.buildings[u32(building)];
	return f64(desc.rate) * std::pow(f64(desc.growth), f64(std::max(level, 1u) - 1));
}

f64 StorageOf(const Catalog& catalog, u32 depotLevel)
{
	const BuildingDesc& desc = catalog.colony.buildings[u32(Building::Depot)];
	return f64(desc.storage) * std::pow(f64(desc.growth), f64(std::max(depotLevel, 1u) - 1));
}

f64 CommandCap(const Catalog& catalog, u32 commandLevel)
{
	const BuildingDesc& desc = catalog.colony.buildings[u32(Building::Command)];
	return f64(desc.points) + f64(desc.pointsPerLevel) * f64(std::max(commandLevel, 1u) - 1);
}

f64 CommandRefill(const Catalog& catalog, u32 commandLevel)
{
	const BuildingDesc& desc = catalog.colony.buildings[u32(Building::Command)];
	return std::max(1.0, f64(desc.refill) *
	                         std::pow(f64(desc.refillGrowth), f64(std::max(commandLevel, 1u) - 1)));
}

Resources UpgradeCost(const Catalog& catalog, Building building, u32 level)
{
	return Scaled(catalog.colony.buildings[u32(building)].cost,
	              std::pow(f64(catalog.colony.costGrowth), f64(std::max(level, 1u) - 1)));
}

f64 UpgradeSeconds(const Catalog& catalog, u32 level)
{
	return f64(catalog.colony.time) *
	       std::pow(f64(catalog.colony.timeGrowth), f64(std::max(level, 1u) - 1));
}

void Advance(const Catalog& catalog, Account& account, i64 now)
{
	// The upgrades done by now, the earliest first: production at each one's
	// old rates up to its end.
	for (;;)
	{
		i32 next = -1;
		for (u32 b = 0; b < BUILDINGS; ++b)
		{
			const i64 done = account.upgradeDone[b];
			if (done != 0 && done <= now && (next < 0 || done < account.upgradeDone[next]))
				next = i32(b);
		}
		if (next < 0)
			break;
		const i64 done = account.upgradeDone[next];
		if (done > account.updated)
		{
			Produce(catalog, account, f64(done - account.updated));
			account.updated = done;
		}
		++account.levels[next];
		account.upgradeDone[next] = 0;
	}
	if (now > account.updated)
	{
		Produce(catalog, account, f64(now - account.updated));
		account.updated = now;
	}
}

bool Upgrade(const Catalog& catalog, Account& account, Building building, i64 now, std::string& why)
{
	Advance(catalog, account, now);
	if (account.IsUpgrading(building))
	{
		why = "error.busy";
		return false;
	}
	const u32 level = account.levels[u32(building)];
	const Resources cost = UpgradeCost(catalog, building, level);
	if (!CanPay(account, cost))
	{
		why = Lacking(account, cost);
		return false;
	}
	Pay(account, cost);
	// At least a second, so that 0 stays "none".
	account.upgradeDone[u32(building)] =
		std::max(now + i64(std::ceil(UpgradeSeconds(catalog, level))), now + 1);
	return true;
}

bool Build(const Catalog& catalog, Account& account, std::string_view blueprint, i64 now,
           std::string& why)
{
	Advance(catalog, account, now);
	if (!Knows(account, blueprint))
	{
		why = "error.blueprint";
		return false;
	}
	Resources cost;
	const HullDesc* hull = catalog.FindHull(blueprint);
	const ModuleDesc* module = catalog.FindModule(blueprint);
	const AmmoDesc* charge = catalog.FindAmmo(blueprint);
	if (hull)
		cost = hull->cost;
	else if (module)
		cost = module->cost;
	else if (charge)
		cost = charge->cost;
	else
	{
		why = "error.blueprint";
		return false;
	}
	if ((hull && account.ships.size() >= MAX_SHIPS_KEPT) ||
	    (module && account.modules.size() >= MAX_MODULES_KEPT))
	{
		why = "error.full";
		return false;
	}
	if (!CanPay(account, cost))
	{
		why = Lacking(account, cost);
		return false;
	}
	Pay(account, cost);
	if (hull)
	{
		ShipPlan plan;
		plan.hull = hull->id;
		account.ships.push_back(MakeRecord(catalog, account, plan));
	}
	else if (module)
		account.modules.push_back({module->id, 1.0f});
	else
		Add(account.items, charge->id, charge->batch);
	return true;
}

bool FitModule(const Catalog& catalog, Account& account, u32 id, u32 slot, i32 stored,
               std::string& why)
{
	ShipRecord* ship = account.FindShip(id);
	const HullDesc* hull = ship ? catalog.FindHull(ship->fit.hull) : nullptr;
	if (!ship || !hull || ship->away)
	{
		why = "error.ship";
		return false;
	}
	const std::vector<SlotKind> kinds = SlotsOf(*hull);
	if (slot >= kinds.size() || stored >= i32(account.modules.size()))
	{
		why = "error.slot";
		return false;
	}
	ship->fit.slots.resize(kinds.size());
	FittedModule& fitted = ship->fit.slots[slot];
	if (stored >= 0)
	{
		const StoredModule module = account.modules[usize(stored)];
		const ModuleDesc* desc = catalog.FindModule(module.id);
		if (!desc || desc->slot != kinds[slot])
		{
			why = "error.slot";
			return false;
		}
		account.modules.erase(account.modules.begin() + stored);
		if (!fitted.id.empty())
			account.modules.push_back({fitted.id, fitted.condition});
		fitted = {module.id, module.condition};
		return true;
	}
	if (fitted.id.empty())
		return true;
	if (account.modules.size() >= MAX_MODULES_KEPT)
	{
		why = "error.full";
		return false;
	}
	account.modules.push_back({fitted.id, fitted.condition});
	fitted = {};
	return true;
}

bool LoadHold(const Catalog& catalog, Account& account, u32 id, std::string_view item, i32 count,
              std::string& why)
{
	ShipRecord* ship = account.FindShip(id);
	const HullDesc* hull = ship ? catalog.FindHull(ship->fit.hull) : nullptr;
	if (!ship || !hull || ship->away)
	{
		why = "error.ship";
		return false;
	}
	if (!catalog.FindAmmo(item) || count == 0)
	{
		why = "error.item";
		return false;
	}
	if (count < 0)
	{
		const u32 taken = std::min(u32(-i64(count)), CountOf(ship->fit.hold, item));
		Add(ship->fit.hold, item, -i64(taken));
		Add(account.items, item, taken);
		return true;
	}
	if (CountOf(account.items, item) < u32(count))
	{
		why = "error.storage";
		return false;
	}
	f32 volume = 0.0f;
	f32 mass = 0.0f;
	HoldSize(catalog, ship->fit.hold, volume, mass);
	f32 itemVolume = 0.0f;
	f32 itemMass = 0.0f;
	catalog.ItemSize(item, itemVolume, itemMass);
	if (volume + itemVolume * f32(count) > hull->cargo + 1e-3f)
	{
		why = "error.hold";
		return false;
	}
	Add(account.items, item, -i64(count));
	Add(ship->fit.hold, item, count);
	return true;
}

f64 RepairCost(const Catalog& catalog, const ShipRecord& ship)
{
	const HullDesc* hull = catalog.FindHull(ship.fit.hull);
	if (!hull)
		return 0.0;
	f64 points = f64(hull->hull) * (1.0 - std::clamp(f64(ship.fit.condition), 0.0, 1.0));
	for (const FittedModule& fitted : ship.fit.slots)
	{
		if (const ModuleDesc* module = catalog.FindModule(fitted.id))
			points += f64(module->health) * (1.0 - std::clamp(f64(fitted.condition), 0.0, 1.0));
	}
	return std::ceil(points * f64(catalog.rules.repairMetal) - 1e-3);
}

bool Repair(const Catalog& catalog, Account& account, u32 id, i64 now, std::string& why)
{
	Advance(catalog, account, now);
	ShipRecord* ship = account.FindShip(id);
	if (!ship || ship->away)
	{
		why = "error.ship";
		return false;
	}
	Resources cost;
	cost[Resource::Metal] = RepairCost(catalog, *ship);
	if (!CanPay(account, cost))
	{
		why = Lacking(account, cost);
		return false;
	}
	Pay(account, cost);
	ship->fit.condition = 1.0f;
	for (FittedModule& fitted : ship->fit.slots)
		fitted.condition = 1.0f;
	return true;
}

bool IsCleared(const Account& account, Hex hex)
{
	return std::find(account.cleared.begin(), account.cleared.end(), hex) != account.cleared.end();
}

bool CanAttack(const Account& account, Hex hex)
{
	const u32 ring = RingOf(hex);
	if (ring == 0)
		return false;
	if (ring == 1 || IsCleared(account, hex))
		return true;
	for (u32 direction = 0; direction < 6; ++direction)
	{
		if (IsCleared(account, Neighbor(hex, direction)))
			return true;
	}
	return false;
}

bool IsVisible(const Catalog& catalog, const Account& account, Hex hex)
{
	if (RingOf(hex) <= catalog.map.fog)
		return true;
	for (const Hex cleared : account.cleared)
	{
		if (Distance(cleared, hex) <= catalog.map.fog)
			return true;
	}
	return false;
}

LaunchCost CostOfLaunch(const Catalog& catalog, Hex hex, u32 shipCount)
{
	LaunchCost cost;
	cost.command = catalog.map.launchCommand;
	cost.fuel = f64(shipCount) * (catalog.map.fuel + catalog.map.fuelPerRing * f64(RingOf(hex)));
	return cost;
}

bool Launch(const Catalog& catalog, Account& account, Hex hex, const std::vector<u32>& ships,
            i64 now, std::string& why)
{
	Advance(catalog, account, now);
	if (!CanAttack(account, hex))
	{
		why = "error.node";
		return false;
	}
	if (ships.empty() || ships.size() > catalog.rules.maxFleet)
	{
		why = "error.fleet";
		return false;
	}
	for (usize i = 0; i < ships.size(); ++i)
	{
		const ShipRecord* ship = account.FindShip(ships[i]);
		Ship made;
		if (!ship || ship->away || std::count(ships.begin(), ships.end(), ships[i]) > 1 ||
		    !MakeShip(catalog, ship->fit, made))
		{
			why = "error.ship";
			return false;
		}
	}
	const LaunchCost cost = CostOfLaunch(catalog, hex, u32(ships.size()));
	if (account.command + 1e-6 < cost.command)
	{
		why = "error.command";
		return false;
	}
	Resources fuel;
	fuel[Resource::He3] = cost.fuel;
	if (!CanPay(account, fuel))
	{
		why = "error.he3";
		return false;
	}
	account.command -= cost.command;
	Pay(account, fuel);
	for (const u32 id : ships)
		account.FindShip(id)->away = true;
	++account.stats.battles;
	return true;
}

Loot RollLoot(const Catalog& catalog, const Node& node, u32& random)
{
	const LootDesc& desc = catalog.map.loot;
	Loot loot;
	const f64 times = f64(node.loot) * std::pow(f64(desc.growth), f64(std::max(node.ring, 1u) - 1));
	for (u32 r = 0; r < RESOURCES; ++r)
	{
		const f64 spread = 1.0 + f64(desc.spread) * (2.0 * f64(Next(random)) - 1.0);
		loot.resources.amount[r] = std::floor(desc.base.amount[r] * times * spread);
	}
	if (Next(random) < desc.ammoChance && !desc.ammo.id.empty())
		loot.items.push_back(desc.ammo);
	if (Next(random) < desc.moduleChance && !desc.modules.empty())
		loot.items.push_back(
			{desc.modules[u32(Next(random) * f32(desc.modules.size())) % desc.modules.size()], 1});
	return loot;
}

void LootSize(const Catalog& catalog, const Loot& loot, f32& volume, f32& mass)
{
	std::vector<Stack> stacks;
	AddLoot(stacks, loot);
	HoldSize(catalog, stacks, volume, mass);
}

void AddLoot(std::vector<Stack>& into, const Loot& loot)
{
	for (u32 r = 0; r < RESOURCES; ++r)
	{
		if (loot.resources.amount[r] >= 1.0)
			Add(into, ResourceId(Resource(r)), i64(loot.resources.amount[r]));
	}
	for (const Stack& stack : loot.items)
		Add(into, stack.id, stack.count);
}

void ApplyOutcome(const Catalog& catalog, Account& account, const BattleOutcome& outcome, i64 now)
{
	Advance(catalog, account, now);
	for (const ShipOutcome& result : outcome.ships)
	{
		const auto ship = std::find_if(account.ships.begin(), account.ships.end(),
		                               [&](const ShipRecord& s) { return s.id == result.id; });
		if (ship == account.ships.end())
			continue;
		if (result.lost)
		{
			account.ships.erase(ship);
			++account.stats.lost;
			continue;
		}
		ship->away = false;
		ship->fit.condition = std::clamp(result.condition, 0.01f, 1.0f);
		for (usize i = 0; i < ship->fit.slots.size() && i < result.modules.size(); ++i)
			ship->fit.slots[i].condition = std::clamp(result.modules[i], 0.0f, 1.0f);
		// Resources to the colony, modules to storage, ammo stays aboard.
		ship->fit.hold.clear();
		for (const Stack& stack : result.hold)
		{
			Resource resource;
			if (FindResource(stack.id, resource))
				account.resources[resource] += f64(stack.count);
			else if (catalog.FindModule(stack.id))
			{
				for (u32 n = 0; n < stack.count && account.modules.size() < MAX_MODULES_KEPT; ++n)
					account.modules.push_back({stack.id, 1.0f});
			}
			else if (catalog.FindAmmo(stack.id))
				Add(ship->fit.hold, stack.id, stack.count);
		}
	}
	account.stats.kills += outcome.kills;
	if (outcome.cleared)
	{
		++account.stats.won;
		if (!IsCleared(account, outcome.node))
			account.cleared.push_back(outcome.node);
		const u32 ring = RingOf(outcome.node);
		if (ring > account.deepest)
		{
			account.deepest = ring;
			account.deepestAt = now;
		}
	}
	Rescue(catalog, account);
}

void BringHome(Account& account)
{
	for (ShipRecord& ship : account.ships)
		ship.away = false;
}

std::string ToJson(const Account& account, bool withKey)
{
	Builder out;
	yyjson_mut_val* root = out.Object();
	out.SetInt(root, "version", ACCOUNT_VERSION);
	if (withKey)
	{
		out.Set(root, "key", std::string_view(account.key));
		yyjson_mut_val* keys = out.Array();
		for (const std::string& key : account.keys)
			out.AppendText(keys, key);
		out.Set(root, "keys", keys);
	}
	yyjson_mut_val* identities = out.Array();
	for (const Identity& identity : account.identities)
	{
		yyjson_mut_val* entry = out.Object();
		out.Set(entry, "provider", std::string_view(identity.provider));
		out.Set(entry, "id", std::string_view(identity.id));
		out.Set(entry, "name", std::string_view(identity.name));
		out.Append(identities, entry);
	}
	out.Set(root, "identities", identities);
	if (account.banned)
		out.Set(root, "banned", true);
	if (account.hidden)
		out.Set(root, "hidden", true);
	if (account.kept)
		out.Set(root, "kept", true);
	out.Set(root, "name", std::string_view(account.name));
	out.SetInt(root, "created", account.created);
	out.SetInt(root, "updated", account.updated);
	yyjson_mut_val* resources = out.Object();
	for (u32 r = 0; r < RESOURCES; ++r)
		out.Set(resources, ResourceId(Resource(r)), account.resources.amount[r]);
	out.Set(root, "resources", resources);
	out.Set(root, "command", account.command);
	yyjson_mut_val* levels = out.Object();
	for (u32 b = 0; b < BUILDINGS; ++b)
		out.SetInt(levels, BuildingId(Building(b)), account.levels[b]);
	out.Set(root, "levels", levels);
	yyjson_mut_val* upgrades = out.Object();
	for (u32 b = 0; b < BUILDINGS; ++b)
	{
		if (account.upgradeDone[b] != 0)
			out.SetInt(upgrades, BuildingId(Building(b)), account.upgradeDone[b]);
	}
	out.Set(root, "upgrades", upgrades);
	out.Set(root, "items", WriteStacks(out, account.items));
	yyjson_mut_val* modules = out.Array();
	for (const StoredModule& module : account.modules)
	{
		yyjson_mut_val* entry = out.Object();
		out.Set(entry, "id", std::string_view(module.id));
		out.Set(entry, "condition", f64(module.condition));
		out.Append(modules, entry);
	}
	out.Set(root, "modules", modules);
	yyjson_mut_val* blueprints = out.Array();
	for (const std::string& id : account.blueprints)
		out.AppendText(blueprints, id);
	out.Set(root, "blueprints", blueprints);
	yyjson_mut_val* ships = out.Array();
	for (const ShipRecord& ship : account.ships)
	{
		yyjson_mut_val* entry = out.Object();
		out.SetInt(entry, "id", ship.id);
		out.Set(entry, "hull", std::string_view(ship.fit.hull));
		out.Set(entry, "condition", f64(ship.fit.condition));
		out.Set(entry, "away", ship.away);
		yyjson_mut_val* slots = out.Array();
		for (const FittedModule& fitted : ship.fit.slots)
		{
			yyjson_mut_val* slot = out.Object();
			out.Set(slot, "id", std::string_view(fitted.id));
			out.Set(slot, "condition", f64(fitted.condition));
			out.Append(slots, slot);
		}
		out.Set(entry, "slots", slots);
		out.Set(entry, "hold", WriteStacks(out, ship.fit.hold));
		out.Append(ships, entry);
	}
	out.Set(root, "ships", ships);
	out.SetInt(root, "nextShip", account.nextShip);
	yyjson_mut_val* cleared = out.Array();
	for (const Hex hex : account.cleared)
	{
		yyjson_mut_val* pair = out.Array();
		out.AppendNumber(pair, hex.q);
		out.AppendNumber(pair, hex.r);
		out.Append(cleared, pair);
	}
	out.Set(root, "cleared", cleared);
	out.SetInt(root, "deepest", account.deepest);
	out.SetInt(root, "deepestAt", account.deepestAt);
	yyjson_mut_val* stats = out.Object();
	out.SetInt(stats, "battles", account.stats.battles);
	out.SetInt(stats, "won", account.stats.won);
	out.SetInt(stats, "lost", account.stats.lost);
	out.SetInt(stats, "kills", account.stats.kills);
	out.Set(root, "stats", stats);
	return out.Write(root);
}

bool FromJson(std::string_view text, Account& account)
{
	json::Document document(text);
	yyjson_val* root = document.Root();
	const u32 version = yyjson_is_obj(root) ? json::Count(root, "version") : 0;
	if (version < 1 || version > ACCOUNT_VERSION)
		return false;
	account = {};
	account.key = json::Text(root, "key");
	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	yyjson_arr_foreach(json::Get(root, "keys"), index, max, value)
	{
		if (account.keys.size() < MAX_KEYS_KEPT && !json::Text(value).empty())
			account.keys.emplace_back(json::Text(value));
	}
	yyjson_arr_foreach(json::Get(root, "identities"), index, max, value)
	{
		Identity identity{std::string(json::Text(value, "provider")),
		                  std::string(json::Text(value, "id")),
		                  std::string(json::Text(value, "name"))};
		if (!identity.provider.empty() && !identity.id.empty())
			account.identities.push_back(std::move(identity));
	}
	account.banned = json::Flag(root, "banned");
	account.hidden = json::Flag(root, "hidden");
	account.kept = json::Flag(root, "kept");
	account.name = json::Text(root, "name");
	account.created = Integer(root, "created");
	account.updated = Integer(root, "updated");
	yyjson_val* resources = json::Get(root, "resources");
	for (u32 r = 0; r < RESOURCES; ++r)
		account.resources.amount[r] =
			std::max(0.0, json::Number(resources, ResourceId(Resource(r))));
	if (version == 1)
		account.resources[Resource::He3] = std::max(0.0, json::Number(resources, "he4"));
	account.command = std::max(0.0, json::Number(root, "command"));
	yyjson_val* levels = json::Get(root, "levels");
	for (u32 b = 0; b < BUILDINGS; ++b)
		account.levels[b] = std::clamp(json::Count(levels, BuildingId(Building(b)), 1), 1u, 1000u);
	yyjson_val* upgrades = json::Get(root, "upgrades");
	for (u32 b = 0; b < BUILDINGS; ++b)
		account.upgradeDone[b] = std::max<i64>(0, Integer(upgrades, BuildingId(Building(b))));
	// Before parallel upgrades: one building at a time.
	Building upgrading;
	if (FindBuilding(json::Text(root, "upgrading"), upgrading))
		account.upgradeDone[u32(upgrading)] = std::max<i64>(0, Integer(root, "upgradeDone"));
	account.items = ReadStacks(json::Get(root, "items"));
	yyjson_arr_foreach(json::Get(root, "modules"), index, max, value)
	{
		account.modules.push_back({std::string(json::Text(value, "id")),
		                           std::clamp(json::Float(value, "condition", 1.0f), 0.0f, 1.0f)});
	}
	yyjson_arr_foreach(json::Get(root, "blueprints"), index, max, value)
	{
		account.blueprints.emplace_back(json::Text(value));
	}
	yyjson_arr_foreach(json::Get(root, "ships"), index, max, value)
	{
		ShipRecord& ship = account.ships.emplace_back();
		ship.id = json::Count(value, "id");
		ship.fit.hull = json::Text(value, "hull");
		ship.fit.condition = std::clamp(json::Float(value, "condition", 1.0f), 0.0f, 1.0f);
		ship.away = json::Flag(value, "away");
		usize slotIndex = 0;
		usize slotMax = 0;
		yyjson_val* slot = nullptr;
		yyjson_arr_foreach(json::Get(value, "slots"), slotIndex, slotMax, slot)
		{
			ship.fit.slots.push_back(
				{std::string(json::Text(slot, "id")),
				 std::clamp(json::Float(slot, "condition", 1.0f), 0.0f, 1.0f)});
		}
		ship.fit.hold = ReadStacks(json::Get(value, "hold"));
	}
	account.nextShip = std::max(1u, json::Count(root, "nextShip", 1));
	yyjson_arr_foreach(json::Get(root, "cleared"), index, max, value)
	{
		if (yyjson_is_arr(value) && yyjson_arr_size(value) == 2)
			account.cleared.push_back({i32(yyjson_get_num(yyjson_arr_get(value, 0))),
			                           i32(yyjson_get_num(yyjson_arr_get(value, 1)))});
	}
	account.deepest = json::Count(root, "deepest");
	account.deepestAt = Integer(root, "deepestAt");
	yyjson_val* stats = json::Get(root, "stats");
	account.stats.battles = json::Count(stats, "battles");
	account.stats.won = json::Count(stats, "won");
	account.stats.lost = json::Count(stats, "lost");
	account.stats.kills = json::Count(stats, "kills");
	return true;
}

std::string ProfileJson(const Account& account, i64 now)
{
	return "{\"now\":" + std::to_string(now) + ",\"account\":" + ToJson(account, false) + "}";
}

bool ReadProfile(std::string_view text, Account& account, i64& now)
{
	json::Document document(text);
	yyjson_val* root = document.Root();
	yyjson_val* body = json::Get(root, "account");
	if (!body)
		return false;
	now = Integer(root, "now");
	usize length = 0;
	char* inner = yyjson_val_write(body, 0, &length);
	const bool read = inner && FromJson(std::string_view(inner, length), account);
	free(inner);
	return read;
}

namespace
{
constexpr const char* OPERATIONS[] = {"refresh", "upgrade", "build",  "fit",
                                      "load",    "repair",  "launch", "return"};
constexpr const char* ENDS[] = {"cleared", "returned", "lost"};
} // namespace

std::string ToJson(const Operation& operation)
{
	Builder out;
	yyjson_mut_val* root = out.Object();
	out.Set(root, "op", std::string_view(OPERATIONS[u32(operation.kind)]));
	switch (operation.kind)
	{
		case Operation::Kind::Refresh:
		case Operation::Kind::Return: break;
		case Operation::Kind::Upgrade:
			out.Set(root, "building", std::string_view(BuildingId(operation.building)));
			break;
		case Operation::Kind::Build: out.Set(root, "id", std::string_view(operation.id)); break;
		case Operation::Kind::Fit:
			out.SetInt(root, "ship", operation.ship);
			out.SetInt(root, "slot", operation.slot);
			out.SetInt(root, "module", operation.module);
			break;
		case Operation::Kind::Load:
			out.SetInt(root, "ship", operation.ship);
			out.Set(root, "id", std::string_view(operation.id));
			out.SetInt(root, "count", operation.count);
			break;
		case Operation::Kind::Repair: out.SetInt(root, "ship", operation.ship); break;
		case Operation::Kind::Launch:
		{
			yyjson_mut_val* node = out.Array();
			out.AppendNumber(node, operation.node.q);
			out.AppendNumber(node, operation.node.r);
			out.Set(root, "node", node);
			yyjson_mut_val* ships = out.Array();
			for (const u32 id : operation.ships)
				out.AppendNumber(ships, id);
			out.Set(root, "ships", ships);
			break;
		}
	}
	return out.Write(root);
}

bool FromJson(std::string_view text, Operation& operation)
{
	json::Document document(text);
	yyjson_val* root = document.Root();
	if (!yyjson_is_obj(root))
		return false;
	operation = {};
	const std::string_view op = json::Text(root, "op");
	u32 kind = 0;
	while (kind < std::size(OPERATIONS) && op != OPERATIONS[kind])
		++kind;
	if (kind == std::size(OPERATIONS))
		return false;
	operation.kind = Operation::Kind(kind);
	operation.id = json::Text(root, "id");
	operation.ship = json::Count(root, "ship");
	operation.slot = json::Count(root, "slot");
	operation.module = i32(std::clamp(json::Number(root, "module", -1.0), -1.0, 1e6));
	operation.count = i32(std::clamp(json::Number(root, "count"), -1e6, 1e6));
	if (operation.kind == Operation::Kind::Upgrade &&
	    !FindBuilding(json::Text(root, "building"), operation.building))
		return false;
	yyjson_val* node = json::Get(root, "node");
	if (yyjson_is_arr(node) && yyjson_arr_size(node) == 2)
		operation.node = {i32(std::clamp(yyjson_get_num(yyjson_arr_get(node, 0)), -1e6, 1e6)),
		                  i32(std::clamp(yyjson_get_num(yyjson_arr_get(node, 1)), -1e6, 1e6))};
	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	yyjson_arr_foreach(json::Get(root, "ships"), index, max, value)
	{
		if (yyjson_is_num(value) && operation.ships.size() < 16)
			operation.ships.push_back(u32(std::clamp(yyjson_get_num(value), 0.0, 1e9)));
	}
	return true;
}

std::string ToJson(const BattleReport& report)
{
	Builder out;
	yyjson_mut_val* root = out.Object();
	out.Set(root, "end", std::string_view(ENDS[u32(report.end)]));
	yyjson_mut_val* node = out.Array();
	out.AppendNumber(node, report.node.q);
	out.AppendNumber(node, report.node.r);
	out.Set(root, "node", node);
	out.SetInt(root, "kills", report.kills);
	out.SetInt(root, "lost", report.lost);
	out.Set(root, "loot", WriteStacks(out, report.loot));
	return out.Write(root);
}

bool FromJson(std::string_view text, BattleReport& report)
{
	json::Document document(text);
	yyjson_val* root = document.Root();
	if (!yyjson_is_obj(root))
		return false;
	report = {};
	const std::string_view end = json::Text(root, "end");
	u32 kind = 0;
	while (kind < std::size(ENDS) && end != ENDS[kind])
		++kind;
	if (kind == std::size(ENDS))
		return false;
	report.end = BattleReport::End(kind);
	yyjson_val* node = json::Get(root, "node");
	if (yyjson_is_arr(node) && yyjson_arr_size(node) == 2)
		report.node = {i32(yyjson_get_num(yyjson_arr_get(node, 0))),
		               i32(yyjson_get_num(yyjson_arr_get(node, 1)))};
	report.kills = json::Count(root, "kills");
	report.lost = json::Count(root, "lost");
	report.loot = ReadStacks(json::Get(root, "loot"));
	return true;
}
} // namespace sn::sim
