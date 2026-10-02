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

// Away from the first rock on the ship's way over the next second or so,
// more the nearer it is; nothing when the way is clear.
Vec2 AvoidRocks(const World& world, const Ship& ship)
{
	const f32 speed = Length(ship.velocity);
	const Vec2 way = speed > 2.0f ? ship.velocity * (1.0f / speed) : Forward(ship.angle);
	const f32 look = 8.0f + 1.2f * speed;
	f32 nearest = look;
	Vec2 push = {};
	for (u32 r = 0; r < world.rockCount; ++r)
	{
		const Rock& rock = world.rocks[r];
		if (rock.health <= 0.0f)
			continue;
		const Vec2 off = rock.position - ship.position;
		const f32 along = Dot(off, way);
		const f32 clearance = rock.radius + ship.hull.radius + 3.0f;
		if (along < 0.0f || along >= nearest)
			continue;
		// From the rock's middle to the nearest point of the way.
		const Vec2 aside = way * along - off;
		const f32 apart = Length(aside);
		if (apart >= clearance)
			continue;
		nearest = along;
		const Vec2 side = apart > 1e-3f ? aside * (1.0f / apart) : Vec2{-way.y, way.x};
		push = side * (1.0f + 3.0f * (1.0f - along / look));
	}
	return push;
}

// Away from ships of its own team that come too near.
Vec2 KeepApart(const World& world, ShipHandle handle, const Ship& ship)
{
	constexpr f32 ROOM = 10.0f; // m
	Vec2 push = {};
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		const Ship& other = world.ships[slot];
		if (!world.shipIds[slot] || world.shipIds[slot] == handle || !IsAlive(other) ||
		    other.team != ship.team)
			continue;
		const Vec2 off = ship.position - other.position;
		const f32 distance = Length(off);
		if (distance < ROOM && distance > 1e-3f)
			push = push + off * (1.5f * (1.0f - distance / ROOM) / distance);
	}
	return push;
}
} // namespace

Vec2 Lead(Vec2 from, Vec2 velocity, f32 speed, Vec2 at, Vec2 targetVelocity)
{
	// The target as the shooter sees it: off by `r`, moving at `v`. The time
	// t when a shot at `speed` meets it: |r + v t| = speed t.
	const Vec2 r = at - from;
	const Vec2 v = targetVelocity - velocity;
	const f32 a = Dot(v, v) - speed * speed;
	const f32 b = Dot(r, v);
	const f32 c = Dot(r, r);
	f32 t = -1.0f;
	if (std::fabs(a) < 1e-6f)
		t = b < 0.0f ? -c / (2.0f * b) : -1.0f;
	else
	{
		const f32 discriminant = b * b - a * c;
		if (discriminant >= 0.0f)
		{
			const f32 root = std::sqrt(discriminant);
			const f32 first = (-b - root) / a;
			const f32 second = (-b + root) / a;
			t = first > 0.0f && (second <= 0.0f || first < second) ? first : second;
		}
	}
	return t > 0.0f ? from + r + v * t : at;
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
	// Its aim strays, somewhere else every half second or so.
	pilot.untilAim -= TICK_SECONDS;
	if (pilot.untilAim <= 0.0f)
	{
		pilot.untilAim = 0.4f + 0.4f * Next(pilot.random);
		pilot.stray = (2.0f * Next(pilot.random) - 1.0f) * skill.aimError;
	}
	pilot.passLeft -= TICK_SECONDS;

	Vec2 heading;
	f32 thrust = 1.0f;
	bool fire = false;
	if (target)
	{
		const Vec2 to = target->position - ship->position;
		const f32 distance = Length(to);
		if (pilot.pass == Pilot::Pass::Attack && distance < skill.closest)
		{
			// Past it, and on: away at a slant, to come around again.
			pilot.pass = Pilot::Pass::Away;
			pilot.passLeft = 0.7f + 0.6f * Next(pilot.random);
			const f32 speed = Length(ship->velocity);
			const f32 base = speed > 5.0f ? AngleOf(ship->velocity) : AngleOf(to * -1.0f);
			const f32 side = Next(pilot.random) < 0.5f ? -1.0f : 1.0f;
			pilot.away = base + side * (0.3f + 0.4f * Next(pilot.random));
		}
		else if (pilot.pass == Pilot::Pass::Away &&
		         (pilot.passLeft <= 0.0f || distance > skill.range))
			pilot.pass = Pilot::Pass::Attack;

		if (pilot.pass == Pilot::Pass::Away)
			heading = Forward(pilot.away);
		else
		{
			const Vec2 lead = Lead(ship->position, ship->velocity, ship->weapon.speed,
			                       target->position, target->velocity);
			const f32 aim = AngleOf(lead - ship->position) + pilot.stray;
			const f32 off = std::fabs(WrapAngle(aim - ship->angle));
			heading = Forward(aim);
			// Full ahead from afar; slower within range, for a longer run.
			thrust = off > 0.5f ? 0.35f : distance < 0.6f * skill.range ? 0.45f : 1.0f;
			// Once its nose is on the lead (as it strays), within range.
			fire = distance < skill.range &&
			       off < 0.07f + 1.3f * std::atan2(target->hull.radius, std::max(distance, 1.0f));
		}
	}
	else
	{
		// Nobody in sight: roams from point to point, the first the middle.
		pilot.pass = Pilot::Pass::Attack;
		if (DistanceSquared(pilot.roam, ship->position) < 20.0f * 20.0f)
			pilot.roam = ship->position + Forward(2.0f * ph::PI * Next(pilot.random)) *
			                                  (40.0f + 60.0f * Next(pilot.random));
		const Vec2 way = pilot.roam - ship->position;
		heading = way * (1.0f / std::max(Length(way), 1e-3f));
		thrust = 0.6f;
	}

	Vec2 wanted = heading + AvoidRocks(world, *ship) + KeepApart(world, handle, *ship);
	if (Dot(wanted, wanted) < 1e-6f)
		wanted = heading;
	const f32 off = WrapAngle(AngleOf(wanted) - ship->angle);
	ShipControls controls;
	// The nose turns at a fixed rate: this reaches the heading exactly when
	// it is near. Turning hard, it pushes less, for a tighter path.
	controls.turn = std::clamp(off / (ship->hull.turnRate * TICK_SECONDS), -1.0f, 1.0f);
	controls.thrust = std::fabs(off) > 1.2f ? std::min(thrust, 0.3f) : thrust;
	controls.fire = fire;
	return controls;
}
} // namespace sn::bots
