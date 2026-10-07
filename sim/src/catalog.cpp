#include "json.h"

#include <sn/sim/catalog.h>

#include <ph/core/assert.h>
#include <ph/core/log.h>

#include <cmath>
#include <string_view>

namespace sn::sim
{
namespace detail
{
// content/catalog.json, as CMake built it in (catalog_data.cpp).
extern const ph::u8 CATALOG_JSON[];
extern const ph::usize CATALOG_JSON_SIZE;
} // namespace detail

namespace
{
using json::Count;
using json::Float;
using json::Get;
using json::Text;

constexpr const char* RESOURCE_IDS[RESOURCES] = {"metal", "he3", "chips"};
constexpr const char* BUILDING_IDS[BUILDINGS] = {"mine", "extractor", "fab", "depot", "command"};
constexpr const char* DAMAGE_IDS[DAMAGE_TYPES] = {"em", "explosive", "kinetic", "thermal"};
constexpr f32 DEGREES = 3.14159265f / 180.0f;

void ReadDamage(yyjson_val* object, f32 (&amount)[DAMAGE_TYPES])
{
	for (u32 t = 0; t < DAMAGE_TYPES; ++t)
		amount[t] = Float(object, DAMAGE_IDS[t]);
}

Resources ReadResources(yyjson_val* object)
{
	Resources resources;
	for (u32 r = 0; r < RESOURCES; ++r)
		resources.amount[r] = json::Number(object, RESOURCE_IDS[r]);
	return resources;
}

// {"id": count, ...}
std::vector<Stack> ReadStacks(yyjson_val* object)
{
	std::vector<Stack> stacks;
	if (!yyjson_is_obj(object))
		return stacks;
	usize index = 0;
	usize max = 0;
	yyjson_val* key = nullptr;
	yyjson_val* value = nullptr;
	yyjson_obj_foreach(object, index, max, key, value)
	{
		const double count = yyjson_is_num(value) ? yyjson_get_num(value) : 0.0;
		if (count > 0.0)
			stacks.push_back({std::string(Text(key)), u32(count)});
	}
	return stacks;
}

std::vector<std::string> ReadIds(yyjson_val* array)
{
	std::vector<std::string> ids;
	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	if (yyjson_is_arr(array))
		yyjson_arr_foreach(array, index, max, value) { ids.emplace_back(Text(value)); }
	return ids;
}

ShipPlan ReadPlan(yyjson_val* object)
{
	ShipPlan plan;
	plan.hull = Text(object, "hull");
	plan.modules = ReadIds(Get(object, "modules"));
	plan.hold = ReadStacks(Get(object, "hold"));
	return plan;
}

bool ReadSize(std::string_view text, Size& size)
{
	constexpr std::string_view NAMES[] = {"S", "M", "L", "XL"};
	for (u32 i = 0; i < 4; ++i)
	{
		if (text == NAMES[i])
		{
			size = Size(i);
			return true;
		}
	}
	return false;
}

bool ReadSlot(std::string_view text, SlotKind& slot)
{
	constexpr std::string_view NAMES[] = {"external", "internal", "rig"};
	for (u32 i = 0; i < SLOT_KINDS; ++i)
	{
		if (text == NAMES[i])
		{
			slot = SlotKind(i);
			return true;
		}
	}
	return false;
}

bool ReadKind(std::string_view text, ModuleKind& kind)
{
	constexpr std::string_view NAMES[] = {"", "laser", "plasma", "shieldBooster",
	                                      "capacitorBattery"};
	for (u32 i = 1; i <= LAST_MODULE_KIND; ++i)
	{
		if (text == NAMES[i])
		{
			kind = ModuleKind(i);
			return true;
		}
	}
	return false;
}

bool ReadHull(yyjson_val* object, HullDesc& hull, std::string& error)
{
	hull.id = Text(object, "id");
	hull.shipClass = Text(object, "class");
	hull.faction = Text(object, "faction");
	if (hull.id.empty() || !ReadSize(Text(object, "size"), hull.size))
	{
		error = "a hull without an id or a size: " + hull.id;
		return false;
	}
	hull.reactor = Float(object, "reactor");
	hull.mass = Float(object, "mass");
	hull.maxSpeed = Float(object, "maxSpeed");
	hull.thrustForward = Float(object, "thrustForward");
	hull.thrustBackward = Float(object, "thrustBackward");
	hull.thrustTurn = Float(object, "thrustTurn");
	hull.hull = Float(object, "hull");
	hull.shield = Float(object, "shield");
	hull.shieldRegen = Float(object, "shieldRegen");
	hull.shieldThreshold = Float(object, "shieldThreshold");
	hull.capacitor = Float(object, "capacitor");
	yyjson_val* slots = Get(object, "slots");
	hull.slots[u32(SlotKind::External)] = Count(slots, "external");
	hull.slots[u32(SlotKind::Internal)] = Count(slots, "internal");
	hull.slots[u32(SlotKind::Rig)] = Count(slots, "rig");
	ReadDamage(Get(object, "resist"), hull.resist);
	hull.scanner = Float(object, "scanner");
	hull.cargo = Float(object, "cargo");
	hull.cargoMassFactor = Float(object, "cargoMassFactor", 1.0f);
	hull.radius = Float(object, "radius", 1.5f);
	hull.cost = ReadResources(Get(object, "cost"));
	if (hull.mass <= 0.0f || hull.hull <= 0.0f ||
	    hull.slots[0] + hull.slots[1] + hull.slots[2] > MAX_MODULES)
	{
		error = "hull " + hull.id + ": no mass, no hull, or too many slots";
		return false;
	}
	return true;
}

bool ReadModule(yyjson_val* object, ModuleDesc& module, std::string& error)
{
	module.id = Text(object, "id");
	if (module.id.empty() || !ReadKind(Text(object, "kind"), module.kind) ||
	    !ReadSlot(Text(object, "slot"), module.slot) ||
	    !ReadSize(Text(object, "size"), module.size))
	{
		error = "module " + module.id + ": a missing or unknown kind, slot or size";
		return false;
	}
	module.health = Float(object, "health");
	module.mass = Float(object, "mass");
	module.volume = Float(object, "volume", 1.0f);
	module.power = Float(object, "power");
	module.energy = Float(object, "energy");
	module.range = Float(object, "range");
	module.turnSpeed = Float(object, "turnSpeed");
	module.fireRate = Float(object, "fireRate");
	module.magazine = Float(object, "magazine");
	module.reload = Float(object, "reload");
	module.shotSpeed = Float(object, "shotSpeed");
	module.shotRadius = Float(object, "shotRadius", 0.3f);
	ReadDamage(Get(object, "dps"), module.dps.amount);
	ReadDamage(Get(object, "factor"), module.factor);
	module.ammo = Text(object, "ammo");
	module.boost = Float(object, "boost");
	module.capacitor = Float(object, "capacitor");
	module.cost = ReadResources(Get(object, "cost"));
	if (module.health <= 0.0f)
	{
		error = "module " + module.id + ": no health";
		return false;
	}
	if (module.kind == ModuleKind::Plasma &&
	    (module.fireRate <= 0.0f || module.shotSpeed <= 0.0f || module.ammo.empty()))
	{
		error = "module " + module.id + ": a gun needs a fire rate, a shot speed and ammo";
		return false;
	}
	return true;
}

bool ReadColony(yyjson_val* object, ColonyDesc& colony, std::string& error)
{
	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	bool seen[BUILDINGS] = {};
	yyjson_arr_foreach(Get(object, "buildings"), index, max, value)
	{
		Building building;
		if (!FindBuilding(Text(value, "id"), building))
		{
			error = "an unknown building: " + std::string(Text(value, "id"));
			return false;
		}
		BuildingDesc& desc = colony.buildings[u32(building)];
		seen[u32(building)] = true;
		Resource produces = Resource::Metal;
		if (FindResource(Text(value, "produces"), produces))
			desc.produces = produces;
		desc.rate = Float(value, "rate");
		desc.growth = Float(value, "growth", 1.0f);
		desc.storage = Float(value, "storage");
		desc.points = Float(value, "points");
		desc.pointsPerLevel = Float(value, "pointsPerLevel");
		desc.refill = Float(value, "refill");
		desc.refillGrowth = Float(value, "refillGrowth", 1.0f);
		desc.cost = ReadResources(Get(value, "cost"));
	}
	for (u32 b = 0; b < BUILDINGS; ++b)
	{
		if (!seen[b])
		{
			error = std::string("the colony lacks its ") + BUILDING_IDS[b];
			return false;
		}
	}
	colony.costGrowth = Float(object, "costGrowth", 1.7f);
	colony.time = Float(object, "time", 30.0f);
	colony.timeGrowth = Float(object, "timeGrowth", 2.0f);
	yyjson_val* start = Get(object, "start");
	colony.startResources = ReadResources(Get(start, "resources"));
	yyjson_arr_foreach(Get(start, "ships"), index, max, value)
	{
		colony.startShips.push_back(ReadPlan(value));
	}
	colony.startModules = ReadIds(Get(start, "modules"));
	colony.startItems = ReadStacks(Get(start, "items"));
	colony.blueprints = ReadIds(Get(start, "blueprints"));
	colony.rescue = ReadPlan(Get(object, "rescue"));
	return true;
}

void ReadMap(yyjson_val* object, MapDesc& map)
{
	map.seed = Count(object, "seed", 1);
	map.fog = Count(object, "fog", 2);
	yyjson_val* launch = Get(object, "launch");
	map.launchCommand = Float(launch, "command", 1.0f);
	map.fuel = Float(launch, "fuel");
	map.fuelPerRing = Float(launch, "fuelPerRing");
	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	yyjson_arr_foreach(Get(object, "kinds"), index, max, value)
	{
		map.kinds.push_back({std::string(Text(value, "id")), Float(value, "weight", 1.0f),
		                     Float(value, "enemies", 1.0f), Float(value, "loot", 1.0f)});
	}
	yyjson_val* loot = Get(object, "loot");
	map.loot.base = ReadResources(loot);
	map.loot.growth = Float(loot, "growth", 1.0f);
	map.loot.spread = Float(loot, "spread", 0.5f);
	yyjson_val* ammo = Get(loot, "ammo");
	map.loot.ammo = {std::string(Text(ammo, "id")), Count(ammo, "count")};
	map.loot.ammoChance = Float(ammo, "chance");
	yyjson_val* modules = Get(loot, "module");
	map.loot.moduleChance = Float(modules, "chance");
	map.loot.modules = ReadIds(Get(modules, "ids"));
	yyjson_val* field = Get(object, "field");
	map.field.count = Count(field, "count", map.field.count);
	map.field.size = Float(field, "size", map.field.size);
	map.field.clearing = Float(field, "clearing", map.field.clearing);
	map.field.minRadius = Float(field, "minRadius", map.field.minRadius);
	map.field.maxRadius = Float(field, "maxRadius", map.field.maxRadius);
	map.field.spacing = Float(field, "spacing", map.field.spacing);
	yyjson_val* distance = Get(object, "enemyDistance");
	if (yyjson_is_arr(distance) && yyjson_arr_size(distance) == 2)
	{
		map.enemyNear = f32(yyjson_get_num(yyjson_arr_get(distance, 0)));
		map.enemyFar = f32(yyjson_get_num(yyjson_arr_get(distance, 1)));
	}
}

bool KnownItem(const Catalog& catalog, std::string_view id)
{
	f32 volume = 0.0f;
	f32 mass = 0.0f;
	return catalog.ItemSize(id, volume, mass);
}

// A plan's hull, modules and hold are in the catalog, and its modules fit.
bool CheckPlan(const Catalog& catalog, const ShipPlan& plan, const char* what, std::string& error)
{
	const HullDesc* hull = catalog.FindHull(plan.hull);
	if (!hull)
	{
		error = std::string(what) + ": no hull " + plan.hull;
		return false;
	}
	u32 used[SLOT_KINDS] = {};
	for (const std::string& id : plan.modules)
	{
		const ModuleDesc* module = catalog.FindModule(id);
		if (!module || ++used[u32(module->slot)] > hull->slots[u32(module->slot)])
		{
			error = std::string(what) + ": module " + id + " is unknown or has no slot";
			return false;
		}
	}
	for (const Stack& stack : plan.hold)
	{
		if (!KnownItem(catalog, stack.id))
		{
			error = std::string(what) + ": an unknown item " + stack.id;
			return false;
		}
	}
	return true;
}

bool CheckReferences(const Catalog& catalog, std::string& error)
{
	for (const ModuleDesc& module : catalog.modules)
	{
		if (!module.ammo.empty() && !catalog.FindAmmo(module.ammo))
		{
			error = "module " + module.id + ": no ammo " + module.ammo;
			return false;
		}
	}
	for (const ShipPlan& plan : catalog.enemies.fits)
	{
		if (!CheckPlan(catalog, plan, "an enemy", error))
			return false;
	}
	for (const ShipPlan& plan : catalog.colony.startShips)
	{
		if (!CheckPlan(catalog, plan, "a starting ship", error))
			return false;
	}
	if (!CheckPlan(catalog, catalog.colony.rescue, "the rescue ship", error))
		return false;
	for (const std::string& id : catalog.colony.startModules)
	{
		if (!catalog.FindModule(id))
		{
			error = "a starting module: " + id;
			return false;
		}
	}
	for (const Stack& stack : catalog.colony.startItems)
	{
		if (!KnownItem(catalog, stack.id))
		{
			error = "a starting item: " + stack.id;
			return false;
		}
	}
	for (const std::string& id : catalog.colony.blueprints)
	{
		if (!catalog.FindHull(id) && !catalog.FindModule(id) && !catalog.FindAmmo(id))
		{
			error = "a blueprint of nothing: " + id;
			return false;
		}
	}
	for (const std::string& id : catalog.map.loot.modules)
	{
		if (!catalog.FindModule(id))
		{
			error = "loot: no module " + id;
			return false;
		}
	}
	if (catalog.map.loot.ammoChance > 0.0f && !catalog.FindAmmo(catalog.map.loot.ammo.id))
	{
		error = "loot: no ammo " + catalog.map.loot.ammo.id;
		return false;
	}
	if (catalog.enemies.fits.empty() || catalog.map.kinds.empty())
	{
		error = "no enemies or no kinds of node";
		return false;
	}
	return true;
}
} // namespace

const char* ResourceId(Resource resource) { return RESOURCE_IDS[u32(resource)]; }

bool FindResource(std::string_view id, Resource& resource)
{
	for (u32 r = 0; r < RESOURCES; ++r)
	{
		if (id == RESOURCE_IDS[r])
		{
			resource = Resource(r);
			return true;
		}
	}
	return false;
}

const char* BuildingId(Building building) { return BUILDING_IDS[u32(building)]; }

bool FindBuilding(std::string_view id, Building& building)
{
	for (u32 b = 0; b < BUILDINGS; ++b)
	{
		if (id == BUILDING_IDS[b])
		{
			building = Building(b);
			return true;
		}
	}
	return false;
}

const HullDesc* Catalog::FindHull(std::string_view id) const
{
	for (const HullDesc& hull : hulls)
	{
		if (hull.id == id)
			return &hull;
	}
	return nullptr;
}

const ModuleDesc* Catalog::FindModule(std::string_view id) const
{
	for (const ModuleDesc& module : modules)
	{
		if (module.id == id)
			return &module;
	}
	return nullptr;
}

const AmmoDesc* Catalog::FindAmmo(std::string_view id) const
{
	for (const AmmoDesc& charge : ammo)
	{
		if (charge.id == id)
			return &charge;
	}
	return nullptr;
}

bool Catalog::ItemSize(std::string_view id, f32& volume, f32& mass) const
{
	Resource resource;
	if (FindResource(id, resource))
	{
		volume = resources[u32(resource)].volume;
		mass = resources[u32(resource)].mass;
		return true;
	}
	if (const AmmoDesc* charge = FindAmmo(id))
	{
		volume = charge->volume;
		mass = charge->mass;
		return true;
	}
	if (const ModuleDesc* module = FindModule(id))
	{
		volume = module->volume;
		mass = module->mass;
		return true;
	}
	return false;
}

u32 HashCatalogText(std::string_view json)
{
	u32 hash = 2166136261u; // FNV-1a
	for (const char c : json)
	{
		if (c == '\r')
			continue;
		hash = (hash ^ u8(c)) * 16777619u;
	}
	return hash;
}

bool ParseCatalog(std::string_view text, Catalog& catalog, std::string& error)
{
	catalog = {};
	json::Document document(text);
	yyjson_val* root = document.Root();
	if (!yyjson_is_obj(root))
	{
		error = "not a JSON object";
		return false;
	}
	catalog.hash = HashCatalogText(text);
	catalog.season = Count(root, "season");

	yyjson_val* rules = Get(root, "rules");
	Rules& r = catalog.rules;
	r.drag = Float(rules, "drag", r.drag);
	r.bounce = Float(rules, "bounce", r.bounce);
	r.combat.moduleDamage = Float(rules, "moduleDamage", r.combat.moduleDamage);
	r.combat.beamAim = Float(rules, "beamAim", 3.0f) * DEGREES;
	r.combat.shotAim = Float(rules, "shotAim", 1.0f) * DEGREES;
	r.combat.boosterCapacitor = Float(rules, "boosterCapacitor", r.combat.boosterCapacitor);
	r.combat.restartCapacitor = Float(rules, "restartCapacitor", r.combat.restartCapacitor);
	r.combat.pickupRange = Float(rules, "pickupRange", r.combat.pickupRange);
	r.attackRange = Float(rules, "attackRange", r.attackRange);
	r.crateLife = Float(rules, "crateLife", r.crateLife);
	r.clearedReturn = Float(rules, "clearedReturn", r.clearedReturn);
	r.repairMetal = Float(rules, "repairMetal", r.repairMetal);
	r.maxFleet = Count(rules, "maxFleet", r.maxFleet);

	usize index = 0;
	usize max = 0;
	yyjson_val* value = nullptr;
	yyjson_arr_foreach(Get(root, "resources"), index, max, value)
	{
		Resource resource;
		if (FindResource(Text(value, "id"), resource))
			catalog.resources[u32(resource)] = {Float(value, "volume"), Float(value, "mass")};
	}
	yyjson_arr_foreach(Get(root, "hulls"), index, max, value)
	{
		if (!ReadHull(value, catalog.hulls.emplace_back(), error))
			return false;
	}
	yyjson_arr_foreach(Get(root, "modules"), index, max, value)
	{
		if (!ReadModule(value, catalog.modules.emplace_back(), error))
			return false;
	}
	yyjson_arr_foreach(Get(root, "ammo"), index, max, value)
	{
		AmmoDesc& charge = catalog.ammo.emplace_back();
		charge.id = Text(value, "id");
		charge.volume = Float(value, "volume");
		charge.mass = Float(value, "mass");
		ReadDamage(Get(value, "damage"), charge.damage.amount);
		charge.cost = ReadResources(Get(value, "cost"));
		charge.batch = std::max(1u, Count(value, "batch", 1));
		if (charge.id.empty() || charge.volume <= 0.0f)
		{
			error = "ammo without an id or a volume: " + charge.id;
			return false;
		}
	}
	yyjson_val* enemies = Get(root, "enemies");
	yyjson_arr_foreach(Get(enemies, "fits"), index, max, value)
	{
		catalog.enemies.fits.push_back(ReadPlan(value));
	}
	catalog.enemies.base = Count(enemies, "base", catalog.enemies.base);
	catalog.enemies.perRings = std::max(1u, Count(enemies, "perRings", catalog.enemies.perRings));
	catalog.enemies.max = Count(enemies, "max", catalog.enemies.max);
	catalog.enemies.growth = Float(enemies, "growth", catalog.enemies.growth);
	catalog.enemies.sight = Float(enemies, "sight", catalog.enemies.sight);
	if (!ReadColony(Get(root, "colony"), catalog.colony, error))
		return false;
	ReadMap(Get(root, "map"), catalog.map);
	return CheckReferences(catalog, error);
}

const Catalog& GetCatalog()
{
	static const Catalog catalog = []
	{
		Catalog read;
		std::string error;
		const std::string_view text(reinterpret_cast<const char*>(detail::CATALOG_JSON),
		                            detail::CATALOG_JSON_SIZE);
		if (!ParseCatalog(text, read, error))
		{
			PH_LOG_ERROR("catalog: %s", error.c_str());
			PH_ASSERT_MSG(false, "content/catalog.json is broken");
		}
		return read;
	}();
	return catalog;
}
} // namespace sn::sim
