#pragma once

#include <sn/sim/world.h>

#include <string>
#include <string_view>
#include <vector>

// The game's numbers (docs/adr/0013-ships-modules-damage.md): hulls, modules,
// ammo, enemies, the colony and the map, read from content/catalog.json,
// which CMake builds into the sim. The website reads the same file. Names
// are strings-table keys made from the ids ("hull.lancer").
namespace sn::sim
{
using ph::f64;

enum class Resource : u8
{
	Metal,
	He3,
	Chips,
};
constexpr u32 RESOURCES = 3;
// "metal", "he3", "chips".
const char* ResourceId(Resource resource);
bool FindResource(std::string_view id, Resource& resource);

// So much of each resource: a cost, a stock, a crate's loot.
struct Resources
{
	f64 amount[RESOURCES] = {};

	f64& operator[](Resource resource) { return amount[u32(resource)]; }
	f64 operator[](Resource resource) const { return amount[u32(resource)]; }
};

// So many of one thing (ammo, a module, a resource) by its id.
struct Stack
{
	std::string id;
	u32 count = 0;
};

enum class Size : u8
{
	S,
	M,
	L,
	XL,
};

enum class SlotKind : u8
{
	External,
	Internal,
	Rig,
};
constexpr u32 SLOT_KINDS = 3;

struct HullDesc
{
	std::string id;
	std::string shipClass; // "frigate"...
	Size size = Size::S;
	std::string faction;        // "colony" (the player's), "pirates"...
	f32 reactor = 0.0f;         // MW
	f32 mass = 0.0f;            // t, empty
	f32 maxSpeed = 0.0f;        // m/s
	f32 thrustForward = 0.0f;   // kN
	f32 thrustBackward = 0.0f;  // kN
	f32 thrustTurn = 0.0f;      // kN
	f32 hull = 0.0f;            // HP
	f32 shield = 0.0f;          // HP
	f32 shieldRegen = 0.0f;     // HP/s
	f32 shieldThreshold = 0.0f; // HP of a kinetic and thermal hit
	f32 capacitor = 0.0f;       // GJ
	u32 slots[SLOT_KINDS] = {};
	f32 resist[DAMAGE_TYPES] = {};
	f32 scanner = 0.0f; // m
	f32 cargo = 0.0f;   // m^3
	f32 cargoMassFactor = 1.0f;
	f32 radius = 1.5f; // m
	Resources cost;
};

struct ModuleDesc
{
	std::string id;
	ModuleKind kind = ModuleKind::None;
	SlotKind slot = SlotKind::External;
	Size size = Size::S;
	f32 health = 0.0f;    // HP
	f32 mass = 0.0f;      // t
	f32 volume = 0.0f;    // m^3, in a hold or storage
	f32 power = 0.0f;     // MW while fitted
	f32 energy = 0.0f;    // GJ/s while on, or GJ a shot (guns)
	f32 range = 0.0f;     // m
	f32 turnSpeed = 0.0f; // rad/s
	f32 fireRate = 0.0f;  // shots/s
	f32 magazine = 0.0f;  // m^3 of ammo it holds
	f32 reload = 0.0f;    // s
	f32 shotSpeed = 0.0f; // m/s
	f32 shotRadius = 0.0f;
	Damage dps;                    // a beam's
	f32 factor[DAMAGE_TYPES] = {}; // a gun's damage: its ammo's times these
	std::string ammo;              // the ammo a gun takes
	f32 boost = 0.0f;              // shield HP/s
	f32 capacitor = 0.0f;          // GJ it adds
	Resources cost;
};

struct AmmoDesc
{
	std::string id;
	f32 volume = 0.0f; // m^3 a charge
	f32 mass = 0.0f;   // t a charge
	Damage damage;     // a shot's, before the gun's factors
	Resources cost;    // a batch's
	u32 batch = 1;     // charges a build makes
};

struct ResourceDesc
{
	f32 volume = 0.0f; // m^3 a unit
	f32 mass = 0.0f;   // t a unit
};

// A ship to make: a hull, its modules (external, then internal, then rigs),
// its hold.
struct ShipPlan
{
	std::string hull;
	std::vector<std::string> modules;
	std::vector<Stack> hold;
};

struct EnemiesDesc
{
	std::vector<ShipPlan> fits;
	u32 base = 2;     // enemies on ring 1
	u32 perRings = 2; // one more every this many rings
	u32 max = 12;
	f32 growth = 1.12f; // their hull, shield and damage, times this a ring
	f32 sight = 260.0f; // m: how far bots see
};

enum class Building : u8
{
	Mine,
	Extractor,
	Fab,
	Depot,
	Command,
};
constexpr u32 BUILDINGS = 5;
// "mine", "extractor", "fab", "depot", "command".
const char* BuildingId(Building building);
bool FindBuilding(std::string_view id, Building& building);

struct BuildingDesc
{
	Resource produces = Resource::Metal; // the mine, the extractor and the fab
	f32 rate = 0.0f;                     // a level 1 producer's, per hour
	f32 growth = 1.0f;                   // rate or storage, times this a level
	f32 storage = 0.0f;                  // the depot's at level 1, of each resource
	f32 points = 0.0f;                   // the command center's command points at level 1
	f32 pointsPerLevel = 0.0f;
	f32 refill = 0.0f;       // s for a point at level 1
	f32 refillGrowth = 1.0f; // refill time, times this a level
	Resources cost;          // level 1's upgrade
};

struct ColonyDesc
{
	BuildingDesc buildings[BUILDINGS];
	f32 costGrowth = 1.7f; // an upgrade's cost, times this a level
	f32 time = 30.0f;      // s for level 1's upgrade
	f32 timeGrowth = 2.0f;
	Resources startResources;
	std::vector<ShipPlan> startShips;
	std::vector<std::string> startModules;
	std::vector<Stack> startItems;
	std::vector<std::string> blueprints;
	ShipPlan rescue; // given when an account has no ship and cannot build one
};

struct NodeKindDesc
{
	std::string id;
	f32 weight = 1.0f;  // how often
	f32 enemies = 1.0f; // their number, times this
	f32 loot = 1.0f;    // crates' loot, times this
};

struct LootDesc
{
	Resources base;    // a crate on ring 1
	f32 growth = 1.0f; // times this a ring
	f32 spread = 0.5f; // each amount between (1 - spread) and (1 + spread) times
	Stack ammo;        // sometimes, so many of it
	f32 ammoChance = 0.0f;
	f32 moduleChance = 0.0f;
	std::vector<std::string> modules;
};

struct MapDesc
{
	u32 seed = 1;
	u32 fog = 2; // rings seen past the explored
	f32 launchCommand = 1.0f;
	f32 fuel = 5.0f;        // He-3 a ship
	f32 fuelPerRing = 1.0f; // more a ship, each ring
	std::vector<NodeKindDesc> kinds;
	LootDesc loot;
	AsteroidFieldDesc field; // its seed is each node's
	f32 enemyNear = 110.0f;  // m from the fleet
	f32 enemyFar = 150.0f;
};

struct Rules
{
	CombatRules combat;
	f32 drag = 0.6f;
	f32 bounce = 0.5f;
	f32 attackRange = 0.8f;    // of its weapon's range, where a ship circles its target
	f32 crateLife = 90.0f;     // s
	f32 clearedReturn = 60.0f; // s after a battle is won
	f32 repairMetal = 0.2f;    // metal a hit point
	u32 maxFleet = 3;
};

struct Catalog
{
	u32 season = 0;
	u32 hash = 0; // of the file: a server and its clients must agree
	Rules rules;
	ResourceDesc resources[RESOURCES];
	std::vector<HullDesc> hulls;
	std::vector<ModuleDesc> modules;
	std::vector<AmmoDesc> ammo;
	EnemiesDesc enemies;
	ColonyDesc colony;
	MapDesc map;

	const HullDesc* FindHull(std::string_view id) const;
	const ModuleDesc* FindModule(std::string_view id) const;
	const AmmoDesc* FindAmmo(std::string_view id) const;
	// The volume and mass of one of anything a hold carries: a resource, a
	// charge of ammo, a module. False for an unknown id.
	bool ItemSize(std::string_view id, f32& volume, f32& mass) const;
};

// A catalog from JSON text, its references checked (ammo, fits, the start).
// False, with what is wrong, otherwise.
bool ParseCatalog(std::string_view json, Catalog& catalog, std::string& error);
// The catalog built into the game, read on first use.
const Catalog& GetCatalog();
// Bytes as the catalog's hash counts them: carriage returns are skipped,
// so that a checkout's line endings do not matter.
u32 HashCatalogText(std::string_view json);
} // namespace sn::sim
