#include <sn/sim/world.h>

#include <algorithm>
#include <cmath>

namespace sn::sim
{
namespace
{
using Handles = ph::HandleAllocator<ShipTag, MAX_SHIPS>;

f32 Length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

void StepShip(Ship& ship)
{
	const HullClass& hull = ship.hull;
	const f32 turn = std::clamp(ship.controls.turn, -1.0f, 1.0f);
	const f32 thrust = std::clamp(ship.controls.thrust, -1.0f, 1.0f);
	ship.angle += turn * hull.turnRate * TICK_SECONDS;
	// Kept in [-pi, pi], so that it stays precise over a long battle.
	if (ship.angle > ph::PI)
		ship.angle -= 2.0f * ph::PI;
	else if (ship.angle < -ph::PI)
		ship.angle += 2.0f * ph::PI;

	const f32 push = thrust > 0.0f ? thrust : thrust * hull.reverse;
	ship.velocity = ship.velocity + Forward(ship.angle) * (push * hull.acceleration * TICK_SECONDS);
	// Arcade space: speed fades without thrust, and has a limit.
	ship.velocity = ship.velocity * std::max(0.0f, 1.0f - hull.drag * TICK_SECONDS);
	const f32 speed = Length(ship.velocity);
	if (speed > hull.maxSpeed)
		ship.velocity = ship.velocity * (hull.maxSpeed / speed);
	ship.position = ship.position + ship.velocity * TICK_SECONDS;
}
} // namespace

Vec2 Forward(f32 angle) { return {-std::sin(angle), std::cos(angle)}; }

ShipHandle SpawnShip(World& world, const Ship& ship)
{
	const ShipHandle handle = world.shipHandles.Allocate();
	if (!handle)
		return {};
	const u32 slot = Handles::Slot(handle);
	world.ships[slot] = ship;
	world.shipIds[slot] = handle;
	return handle;
}

void RemoveShip(World& world, ShipHandle ship)
{
	if (!world.shipHandles.IsValid(ship))
		return;
	world.shipIds[Handles::Slot(ship)] = {};
	world.shipHandles.Free(ship);
}

Ship* GetShip(World& world, ShipHandle ship)
{
	return world.shipHandles.IsValid(ship) ? &world.ships[Handles::Slot(ship)] : nullptr;
}

const Ship* GetShip(const World& world, ShipHandle ship)
{
	return world.shipHandles.IsValid(ship) ? &world.ships[Handles::Slot(ship)] : nullptr;
}

void SetControls(World& world, ShipHandle ship, ShipControls controls)
{
	if (Ship* found = GetShip(world, ship))
		found->controls = controls;
}

void Step(World& world)
{
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		if (world.shipIds[slot])
			StepShip(world.ships[slot]);
	}
	++world.tick;
}
} // namespace sn::sim
