#pragma once

#include <ph/core/handle.h>
#include <ph/core/math.h>

// The simulation (docs/adr/0001-sim-data-model.md, 0003-netcode-model.md):
// plain data in fixed pools, advanced by fixed ticks. The plane of play has
// x to the right and y up the screen (the client maps it to the world's X
// and -Z). Angles are radians, counter-clockwise; 0 faces +y.
namespace sn::sim
{
using ph::f32;
using ph::u32;
using ph::u64;
using ph::Vec2;

constexpr u32 TICK_RATE = 30; // per second; chosen with the scale test (M1.8)
constexpr f32 TICK_SECONDS = 1.0f / f32(TICK_RATE);

// How a hull flies: data, so that blueprints can vary it.
struct HullClass
{
	f32 acceleration = 30.0f; // m/s^2 at full thrust
	f32 reverse = 0.4f;       // reverse thrust, as a share of forward
	f32 maxSpeed = 40.0f;     // m/s
	f32 turnRate = 3.0f;      // rad/s at full turn
	f32 drag = 0.6f;          // share of the speed lost per second without thrust
	f32 radius = 1.5f;        // m, for collisions and picking
};

// What its pilot (a player, an order or a bot) asks of a ship this tick.
struct ShipControls
{
	f32 turn = 0.0f;   // -1 right to 1 left
	f32 thrust = 0.0f; // -1 reverse to 1 forward
};

struct Ship
{
	Vec2 position;
	Vec2 velocity;
	f32 angle = 0.0f;
	HullClass hull;
	ShipControls controls;
};

struct ShipTag;
using ShipHandle = ph::Handle<ShipTag>;
constexpr u32 MAX_SHIPS = 512;

// Trivially copyable: a snapshot or a history entry is a copy.
struct World
{
	u64 tick = 0;
	ph::HandleAllocator<ShipTag, MAX_SHIPS> shipHandles;
	Ship ships[MAX_SHIPS] = {};
	ShipHandle shipIds[MAX_SHIPS] = {}; // each live slot's handle; empty when free
};

ShipHandle SpawnShip(World& world, const Ship& ship);
void RemoveShip(World& world, ShipHandle ship);
// Null for a stale handle.
Ship* GetShip(World& world, ShipHandle ship);
const Ship* GetShip(const World& world, ShipHandle ship);
void SetControls(World& world, ShipHandle ship, ShipControls controls);

// One tick: every ship turns, thrusts and moves.
void Step(World& world);

Vec2 Forward(f32 angle);
} // namespace sn::sim
