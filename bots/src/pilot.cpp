#include <sn/bots/pilot.h>

#include <algorithm>
#include <cmath>

namespace sn::bots
{
namespace
{
using namespace sim;

f32 Next(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return f32(state >> 8) / f32(1u << 24);
}

f32 DistanceSquared(Vec2 a, Vec2 b) { return Dot(a - b, a - b); }

// The ship behind `handle` when it is a live enemy of `ship` within `reach`.
const Ship* Enemy(const World& world, const Ship& ship, ShipHandle handle, f32 reach)
{
	const Ship* other = GetShip(world, handle);
	if (!other || !IsAlive(*other) || other->team == ship.team ||
	    DistanceSquared(other->position, ship.position) > reach * reach)
		return nullptr;
	return other;
}

// The nearest live enemy within `reach`; none when there is none.
ShipHandle Nearest(const World& world, const Ship& ship, f32 reach)
{
	ShipHandle nearest;
	f32 best = reach * reach;
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		const Ship& other = world.ships[slot];
		if (!world.shipIds[slot] || !IsAlive(other) || other.team == ship.team)
			continue;
		const f32 distance = DistanceSquared(other.position, ship.position);
		if (distance <= best)
		{
			best = distance;
			nearest = world.shipIds[slot];
		}
	}
	return nearest;
}
} // namespace

Vec2 KeepApart(const World& world, ShipHandle handle)
{
	constexpr f32 ROOM = 10.0f; // m
	const Ship* ship = GetShip(world, handle);
	if (!ship)
		return {};
	Vec2 push = {};
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		const Ship& other = world.ships[slot];
		if (!world.shipIds[slot] || world.shipIds[slot] == handle || !IsAlive(other) ||
		    other.team != ship->team)
			continue;
		const Vec2 off = ship->position - other.position;
		const f32 distance = Length(off);
		if (distance < ROOM && distance > 1e-3f)
			push = push + off * (12.0f * (1.0f - distance / ROOM) / distance);
	}
	return push;
}

ShipControls Fly(const World& world, ShipHandle handle, Pilot& pilot, const PilotSkill& skill)
{
	const Ship* ship = GetShip(world, handle);
	if (!ship || !IsAlive(*ship))
		return {};

	// A target: kept while it stays in sight, given up for one much nearer.
	pilot.untilLook -= TICK_SECONDS;
	const Ship* target = Enemy(world, *ship, pilot.target, 1.2f * skill.sight);
	if (!target || pilot.untilLook <= 0.0f)
	{
		pilot.untilLook = skill.reaction;
		const ShipHandle nearest = Nearest(world, *ship, skill.sight);
		const Ship* candidate = GetShip(world, nearest);
		if (!target || (candidate && DistanceSquared(candidate->position, ship->position) <
		                                 0.5f * DistanceSquared(target->position, ship->position)))
		{
			pilot.target = nearest;
			target = candidate;
		}
	}
	if (!target)
		pilot.target = {};

	const Vec2 apart = KeepApart(world, handle);
	if (target)
	{
		const Mark mark{target->position, target->velocity};
		Order order;
		order.kind = Order::Kind::Attack;
		return Steer(*ship, order, &mark, AttackDistance(*ship, skill.distance), world.rocks,
		             world.rockCount, apart);
	}
	// Nobody in sight: roams from point to point, the first the middle.
	if (DistanceSquared(pilot.roam, ship->position) < 20.0f * 20.0f)
		pilot.roam = ship->position + Forward(2.0f * ph::PI * Next(pilot.random)) *
		                                  (40.0f + 60.0f * Next(pilot.random));
	Order order;
	order.kind = Order::Kind::Move;
	order.point = pilot.roam;
	return Steer(*ship, order, nullptr, 0.0f, world.rocks, world.rockCount, apart);
}
} // namespace sn::bots
