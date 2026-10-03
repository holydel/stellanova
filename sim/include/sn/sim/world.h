#pragma once

#include <ph/core/handle.h>
#include <ph/core/math.h>

// The simulation (docs/adr/0001-sim-data-model.md, 0003-netcode-model.md,
// 0008-combat-v0.md, 0009-bots-v0.md): plain data in fixed pools, advanced
// by fixed ticks. The plane of play has x to the right and y up the screen
// (the client maps it to the world's X and -Z). Angles are radians,
// counter-clockwise; 0 faces +y. Everything that collides is a circle.
namespace sn::sim
{
using ph::f32;
using ph::u32;
using ph::u64;
using ph::u8;
using ph::Vec2;

constexpr u32 TICK_RATE = 30; // per second; chosen with the scale test (M1.8)
constexpr f32 TICK_SECONDS = 1.0f / f32(TICK_RATE);

// How a hull flies and what it takes: data, so that blueprints can vary it.
struct HullClass
{
	f32 acceleration = 30.0f; // m/s^2 at full thrust
	f32 reverse = 0.4f;       // reverse thrust, as a share of forward
	f32 maxSpeed = 40.0f;     // m/s
	f32 turnRate = 3.0f;      // rad/s at full turn
	f32 drag = 0.6f;          // share of the speed lost per second without thrust
	f32 radius = 1.5f;        // m, for collisions and picking
	f32 bounce = 0.5f;        // share of the speed into a rock kept on the way back
	f32 health = 12.0f;       // damage the hull takes before it breaks apart
	f32 shield = 8.0f;        // damage the shield takes first
	f32 shieldRegen = 2.0f;   // shield back per second, once it rests
	f32 shieldDelay = 2.5f;   // s without hits before the shield comes back
};

// What a ship's gun fires, also data.
struct WeaponClass
{
	f32 interval = 0.12f; // s between shots while the trigger is held
	f32 speed = 90.0f;    // m/s, on top of the ship's own velocity
	f32 life = 1.2f;      // s before a shot fades
	f32 radius = 0.3f;    // m
	f32 damage = 1.0f;
};

// What its pilot (a player, an order or a bot) asks of a ship this tick.
struct ShipControls
{
	f32 turn = 0.0f;   // -1 right to 1 left
	f32 thrust = 0.0f; // -1 reverse to 1 forward
	bool fire = false;
};

// Ships of one team never hit each other: players are 0, bots 1 for now.
constexpr u8 PLAYERS = 0;
constexpr u8 BOTS = 1;

struct Ship
{
	Vec2 position;
	Vec2 velocity;
	f32 angle = 0.0f;
	f32 cooldown = 0.0f; // s until the gun can fire again
	// SpawnShip and ReviveShip fill both from the hull. A ship without
	// health is a wreck: out of play until it is revived or removed.
	f32 health = 0.0f;
	f32 shield = 0.0f;
	f32 rested = 0.0f; // s since the last hit
	u8 team = PLAYERS;
	HullClass hull;
	WeaponClass weapon;
	ShipControls controls;
};

inline bool IsAlive(const Ship& ship) { return ship.health > 0.0f; }

struct ShipTag;
using ShipHandle = ph::Handle<ShipTag>;
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
	f32 damage = 0.0f;
	ShipHandle owner;
	u8 team = PLAYERS; // the owner's: it passes through that team's ships
};
constexpr u32 MAX_SHOTS = 1024;

// What happened in a tick, for the clients' sounds and effects (ADR 0004:
// the match's history is a log of events).
enum class EventType : u8
{
	Fired,      // `ship` fired from `position`
	RockHit,    // a shot of `ship` hit `rock` at `position`
	RockBroken, // `rock` broke apart, last hit by `ship`
	ShipBumped, // `ship` hit a rock (`rock`) or a ship (`other`) at `position`, at `strength` m/s
	ShieldHit,  // a shot of `other` hit `ship`'s shield at `position`, for `strength` damage
	HullHit,    // a shot of `other` hit `ship`'s hull, the shield down
	ShipDestroyed, // `ship` broke apart at `position`, its last hit `other`'s
};
constexpr u8 LAST_EVENT_TYPE = u8(EventType::ShipDestroyed);
constexpr u32 NO_ROCK = ~0u;

struct Event
{
	EventType type = EventType::Fired;
	ShipHandle ship;
	ShipHandle other;
	u32 rock = NO_ROCK;
	Vec2 position;
	f32 strength = 0.0f;
};
constexpr u32 MAX_EVENTS = 256; // a tick's; more are dropped

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
	ph::HandleAllocator<ShipTag, MAX_SHIPS> shipHandles;
	Ship ships[MAX_SHIPS] = {};
	ShipHandle shipIds[MAX_SHIPS] = {}; // each live slot's handle; empty when free
	u32 rockCount = 0;
	Rock rocks[MAX_ROCKS] = {};
	Shot shots[MAX_SHOTS] = {};
	u32 nextShot = 0; // where the search for a free shot starts
	u32 eventCount = 0;
	Event events[MAX_EVENTS] = {}; // the last tick's
};

// A whole ship: its health and shield from its hull.
ShipHandle SpawnShip(World& world, const Ship& ship);
void RemoveShip(World& world, ShipHandle ship);
// A wreck (or any ship) whole again at `position`, still, facing `angle`.
void ReviveShip(World& world, ShipHandle ship, Vec2 position, f32 angle);
// Null for a stale handle; wrecks are there too (IsAlive).
Ship* GetShip(World& world, ShipHandle ship);
const Ship* GetShip(const World& world, ShipHandle ship);
void SetControls(World& world, ShipHandle ship, ShipControls controls);
// For events of the match itself (the server's), after Step.
void AddEvent(World& world, const Event& event);

// Replaces the world's rocks with a field made from the description: the
// same description, the same field.
void MakeAsteroidField(World& world, const AsteroidFieldDesc& desc);
// The hits a rock of that radius takes before it breaks.
f32 RockHealth(f32 radius);

// One tick: every live ship turns, thrusts, moves and fires, and its shield
// rests; shots fly, and the first rock or ship of another team on a shot's
// way takes its damage (a ship's shield first, then its hull); ships bounce
// off rocks and off each other. The tick's events replace the last ones.
void Step(World& world);

// One ship's part of a tick, for Step and for a client that flies its own
// ship ahead of the server (ADR 0003): FlyShip, then FireGun, then
// BounceOffRocks, as Step calls them.
//
// The ship turns, thrusts, slows and moves by its controls and hull.
void FlyShip(Ship& ship);
// While the trigger is held and the gun is ready: the shot it fires now, from
// the nose, and the gun's cooldown starts again. False when it does not
// fire. The shot's owner is the caller's to set.
bool FireGun(Ship& ship, Shot& shot);
// A rock a ship ran into: which, where they touched, and the speed into it.
struct RockBump
{
	u32 rock = NO_ROCK;
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

Vec2 Forward(f32 angle);
// The angle that faces along `direction` (Forward's inverse).
f32 AngleOf(Vec2 direction);
// Into [-pi, pi].
f32 WrapAngle(f32 angle);
f32 Dot(Vec2 a, Vec2 b);
f32 Length(Vec2 v);
} // namespace sn::sim
