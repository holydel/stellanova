#pragma once

#include <ph/core/handle.h>
#include <ph/core/math.h>

// The simulation (docs/adr/0001-sim-data-model.md, 0003-netcode-model.md,
// 0008-combat-v0.md, 0009-bots-v0.md, 0013-ships-modules-damage.md): plain
// data in fixed pools, advanced by fixed ticks. The plane of play has x to
// the right and y up the screen (the client maps it to the world's X and
// -Z). Angles are radians, counter-clockwise; 0 faces +y. Everything that
// collides is a circle. Ships carry modules: turrets that aim and fire on
// their own, and internal ones; the numbers come from the catalog
// (sim/catalog.h, sim/fitting.h).
namespace sn::sim
{
using ph::f32;
using ph::u32;
using ph::u64;
using ph::u8;
using ph::usize;
using ph::Vec2;

constexpr u32 TICK_RATE = 30; // per second; chosen with the scale test (M1.8)
constexpr f32 TICK_SECONDS = 1.0f / f32(TICK_RATE);

// The four kinds of damage (docs/solo-loop.md): EM and explosive go to the
// shield while it lasts; kinetic and thermal give it at most its threshold
// of each hit, and the rest goes to the hull.
enum class DamageType : u8
{
	Em,
	Explosive,
	Kinetic,
	Thermal,
};
constexpr u32 DAMAGE_TYPES = 4;

struct Damage
{
	f32 amount[DAMAGE_TYPES] = {};

	f32 Total() const { return amount[0] + amount[1] + amount[2] + amount[3]; }
};

// How a fitted hull flies and what it takes: data, so that blueprints can
// vary it. sim::MakeShip fills it from the catalog; these defaults are a
// plain frigate, for tests.
struct HullClass
{
	f32 mass = 135.0f;          // t: the hull and its modules; the hold adds to it
	f32 thrust = 3600.0f;       // kN forward: acceleration = thrust / mass
	f32 thrustBack = 1440.0f;   // kN backward
	f32 thrustTurn = 400.0f;    // kN: the turn rate (rad/s) at full turn is this / mass
	f32 maxSpeed = 40.0f;       // m/s
	f32 drag = 0.6f;            // share of the speed lost per second without thrust
	f32 radius = 1.5f;          // m, for collisions and picking
	f32 bounce = 0.5f;          // share of the speed into a rock kept on the way back
	f32 health = 400.0f;        // the hull's hit points: at 0 it breaks apart
	f32 shield = 300.0f;        // hit points the shield takes first
	f32 shieldRegen = 6.0f;     // shield back per second, always
	f32 shieldThreshold = 8.0f; // the most of a kinetic and thermal hit the shield takes
	f32 resist[DAMAGE_TYPES] = {0.0f, 0.1f, 0.2f, 0.1f}; // share of each type taken away
	f32 reactor = 100.0f;                                // MW
	f32 capacitor = 2.0f;                                // GJ, the modules' included
	f32 scanner = 250.0f;                                // m: how far its turrets look for targets
	f32 cargo = 20.0f;                                   // m^3 of hold
	f32 cargoMassFactor = 1.0f;                          // the hold's mass counts this many times
};

enum class ModuleKind : u8
{
	None,
	Laser,            // a beam: damage each tick while on target
	Plasma,           // shots from a magazine that reloads from the hold
	ShieldBooster,    // shield back faster, for energy
	CapacitorBattery, // a larger capacitor (MakeShip adds it to the hull's)
};
constexpr u8 LAST_MODULE_KIND = u8(ModuleKind::CapacitorBattery);

inline bool IsWeapon(ModuleKind kind)
{
	return kind == ModuleKind::Laser || kind == ModuleKind::Plasma;
}

// A fitted module's numbers, from the catalog: data, as hulls are.
struct ModuleClass
{
	ModuleKind kind = ModuleKind::None;
	bool external = false;
	f32 health = 0.0f;    // hit points
	f32 power = 0.0f;     // MW it draws while fitted and working
	f32 energy = 0.0f;    // GJ a second while on (beam, booster), or GJ a shot
	f32 range = 0.0f;     // m
	f32 turnSpeed = 0.0f; // rad/s of its turret
	f32 interval = 0.0f;  // s between shots
	f32 shotSpeed = 0.0f; // m/s, on top of the ship's velocity
	f32 shotRadius = 0.0f;
	u32 magazine = 0;  // shots a load
	f32 reload = 0.0f; // s
	Damage damage;     // a shot's, or a beam's each second
	f32 boost = 0.0f;  // shield a second
};

struct ShipTag;
using ShipHandle = ph::Handle<ShipTag>;

struct Module
{
	ModuleClass type;
	f32 health = 0.0f;    // 0: broken until repaired at the colony
	f32 angle = 0.0f;     // a turret's, from the ship's nose
	f32 cooldown = 0.0f;  // s until it may fire again
	f32 reloading = 0.0f; // s left of a reload
	u32 loaded = 0;       // shots in the magazine
	bool offline = false; // switched off while the reactor is overloaded
	bool working = false; // this tick: a beam on, a booster boosting
	ShipHandle target;    // a turret's, this tick
};
constexpr u32 MAX_MODULES = 4;

// What its pilot (a player, an order or a bot) asks of a ship this tick.
// Turrets aim and fire on their own.
struct ShipControls
{
	f32 turn = 0.0f;   // -1 right to 1 left
	f32 thrust = 0.0f; // -1 reverse to 1 forward
};

// Ships of one team never hit each other: players are 0, bots 1 for now.
constexpr u8 PLAYERS = 0;
constexpr u8 BOTS = 1;

struct Ship
{
	Vec2 position;
	Vec2 velocity;
	f32 angle = 0.0f;
	// SpawnShip and ReviveShip fill these from the hull, unless SpawnShip is
	// given a damaged ship (`health` above 0). A ship without health is a
	// wreck: out of play until it is revived or removed.
	f32 health = 0.0f;
	f32 shield = 0.0f;
	f32 capacitor = 0.0f; // GJ
	u8 team = PLAYERS;
	HullClass hull;
	Module modules[MAX_MODULES] = {};
	u32 moduleCount = 0;
	// The hold: ammo for its guns (charges, a charge's volume and mass) and
	// everything else it carries, as volume and mass.
	u32 ammo = 0;
	f32 ammoVolume = 0.0f; // m^3
	f32 ammoMass = 0.0f;   // t
	f32 cargoUsed = 0.0f;  // m^3, the ammo's included
	f32 cargoMass = 0.0f;  // t, the ammo's included
	ShipControls controls;
	ShipHandle target; // its pilot's choice: its turrets prefer it
};

inline bool IsAlive(const Ship& ship) { return ship.health > 0.0f; }
// The hull, its modules and its hold, as the engines push it.
f32 MassOf(const Ship& ship);
// The longest reach of its working weapons; 0 without any.
f32 WeaponRange(const Ship& ship);

constexpr u32 MAX_SHIPS = 512;

// A rock of an asteroid field: it stays where it is; shots wear it down.
struct Rock
{
	Vec2 position;
	f32 radius = 0.0f; // m
	f32 health = 0.0f; // 0: broken (or never there)
};
constexpr u32 MAX_ROCKS = 1024;

// A gun's shot in flight; free when its life is over.
struct Shot
{
	Vec2 position;
	Vec2 velocity;
	f32 life = 0.0f; // s left
	f32 radius = 0.0f;
	Damage damage;
	ShipHandle owner;
	u8 team = PLAYERS; // the owner's: it passes through that team's ships
};
constexpr u32 MAX_SHOTS = 1024;

// Loot floating where a ship broke apart: a player's ship within reach
// takes it, if its hold has room. What is inside is the server's to know.
struct Crate
{
	Vec2 position;
	f32 life = 0.0f;   // s left; 0: free
	f32 volume = 0.0f; // m^3
	f32 mass = 0.0f;   // t
};
constexpr u32 MAX_CRATES = 128;

// What happened in a tick, for the clients' sounds and effects (ADR 0004:
// the match's history is a log of events). Beams hurt every tick and tell
// no events: snapshots show them.
enum class EventType : u8
{
	Fired,      // `ship` fired from `position`
	RockHit,    // a shot of `ship` hit rock `index` at `position`
	RockBroken, // rock `index` broke apart, last hit by `ship`
	ShipBumped, // `ship` hit a rock (`index`) or a ship (`other`) at `position`, at `strength` m/s
	ShieldHit,  // a shot of `other` hit `ship`'s shield at `position`, for `strength` damage
	HullHit,    // a shot of `other` hit `ship`'s hull, for `strength` damage
	ShipDestroyed, // `ship` broke apart at `position`, its last hit `other`'s
	ModuleBroken,  // `ship`'s module `index` stopped working, hit by `other`
	CrateDropped,  // crate `index` appeared at `position`
	CratePicked,   // `ship` took crate `index`
	CrateLost,     // crate `index` faded away
};
constexpr u8 LAST_EVENT_TYPE = u8(EventType::CrateLost);
constexpr u32 NO_INDEX = ~0u;

struct Event
{
	EventType type = EventType::Fired;
	ShipHandle ship;
	ShipHandle other;
	u32 index = NO_INDEX;
	Vec2 position;
	f32 strength = 0.0f;
};
constexpr u32 MAX_EVENTS = 256; // a tick's; more are dropped

// The catalog's rules that a tick needs (docs/solo-loop.md).
struct CombatRules
{
	f32 moduleDamage = 0.5f;     // of a hull hit, to one external module
	f32 beamAim = 0.0524f;       // rad: a beam fires this near its target
	f32 shotAim = 0.0175f;       // rad: a gun fires this near its lead
	f32 boosterCapacitor = 0.2f; // share of the capacitor a booster keeps
	f32 restartCapacitor = 0.5f; // share at which overloaded modules come back
	f32 pickupRange = 6.0f;      // m from a crate to a ship's edge
};

// A field of rocks around the origin, with room to start in the middle.
struct AsteroidFieldDesc
{
	u32 seed = 1;
	u32 count = 800;
	f32 size = 600.0f;    // m: the field's square
	f32 clearing = 20.0f; // m around the origin without rocks
	f32 minRadius = 1.5f; // m
	f32 maxRadius = 7.0f; // m
	f32 spacing = 3.0f;   // m at least between two rocks
};

// Trivially copyable: a snapshot or a history entry is a copy.
struct World
{
	u64 tick = 0;
	CombatRules rules;
	u32 random = 1; // which module a hull hit hurts
	ph::HandleAllocator<ShipTag, MAX_SHIPS> shipHandles;
	Ship ships[MAX_SHIPS] = {};
	ShipHandle shipIds[MAX_SHIPS] = {}; // each live slot's handle; empty when free
	u32 rockCount = 0;
	Rock rocks[MAX_ROCKS] = {};
	Shot shots[MAX_SHOTS] = {};
	u32 nextShot = 0; // where the search for a free shot starts
	Crate crates[MAX_CRATES] = {};
	u32 eventCount = 0;
	Event events[MAX_EVENTS] = {}; // the last tick's
};

// A ship whole (health, shield and capacitor from its hull), or as damaged
// as it comes when its `health` is above 0; its magazines loaded from its
// hold.
ShipHandle SpawnShip(World& world, const Ship& ship);
void RemoveShip(World& world, ShipHandle ship);
// A wreck (or any ship) whole again at `position`, still, facing `angle`;
// its modules mended.
void ReviveShip(World& world, ShipHandle ship, Vec2 position, f32 angle);
// Null for a stale handle; wrecks are there too (IsAlive).
Ship* GetShip(World& world, ShipHandle ship);
const Ship* GetShip(const World& world, ShipHandle ship);
void SetControls(World& world, ShipHandle ship, ShipControls controls);
// The ship its turrets prefer (none: the nearest in range).
void SetTarget(World& world, ShipHandle ship, ShipHandle target);
// For events of the match itself (the server's), after Step.
void AddEvent(World& world, const Event& event);
// A crate at `position` for `life` seconds; its index, or NO_INDEX when the
// pool is full. Tells CrateDropped.
u32 DropCrate(World& world, Vec2 position, f32 volume, f32 mass, f32 life);

// Replaces the world's rocks with a field made from the description: the
// same description, the same field.
void MakeAsteroidField(World& world, const AsteroidFieldDesc& desc);
// The hits a rock of that radius takes before it breaks.
f32 RockHealth(f32 radius);

// One tick: every live ship turns, thrusts, moves; its capacitor charges;
// its turrets turn and fire, its booster boosts, its shield regenerates.
// Shots fly, and the first rock or ship of another team on a shot's way
// takes its damage; beams hurt the first thing on their line. Ships bounce
// off rocks and off each other, and players' ships take crates within
// reach. The tick's events replace the last ones.
void Step(World& world);

// One ship's flight, for Step and for a client that flies its own ship
// ahead of the server (ADR 0003): FlyShip, then BounceOffRocks, as Step
// calls them.
//
// The ship turns, thrusts, slows and moves by its controls and hull.
void FlyShip(Ship& ship);
// A rock a ship ran into: which, where they touched, and the speed into it.
struct RockBump
{
	u32 rock = NO_INDEX;
	Vec2 at;
	f32 into = 0.0f;
};
// A ship that overlaps rocks is put back on their edges, and its speed into
// each turns back, partly. Writes up to `capacity` bumps to `bumps` (which
// may be null); returns how many there were.
u32 BounceOffRocks(Ship& ship, const Rock* rocks, u32 rockCount, RockBump* bumps, u32 capacity);
// How far along the way from `from` to `from + path` (0 to 1) a point first
// comes within `reach` of `center`; negative when it does not.
f32 SweepContact(Vec2 from, Vec2 path, Vec2 center, f32 reach);
// Where to aim from `from`, moving at `velocity`, so that a shot at `speed`
// (on top of that velocity) meets a target at `at` moving at
// `targetVelocity`; `at` itself when no shot can catch it.
Vec2 Lead(Vec2 from, Vec2 velocity, f32 speed, Vec2 at, Vec2 targetVelocity);
// A hit's damage once resistances took their share, split between shield
// and hull by the rules above. `shield` is what the shield has left.
struct DamageSplit
{
	f32 shield = 0.0f;
	f32 hull = 0.0f;
};
DamageSplit SplitDamage(const HullClass& hull, f32 shield, const Damage& damage);

Vec2 Forward(f32 angle);
// The angle that faces along `direction` (Forward's inverse).
f32 AngleOf(Vec2 direction);
// Into [-pi, pi].
f32 WrapAngle(f32 angle);
f32 Dot(Vec2 a, Vec2 b);
f32 Length(Vec2 v);
} // namespace sn::sim
