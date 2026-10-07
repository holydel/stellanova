#include <sn/bots/orders.h>

#include <algorithm>
#include <cmath>

namespace sn::bots
{
namespace
{
using namespace sim;

constexpr f32 ARRIVED = 1.5f;     // m: close enough to a point
constexpr f32 ORBIT_SPEED = 0.6f; // of the top speed, around a target
constexpr f32 TURN_TIME = 0.2f;   // s to bring the nose round to a new heading
constexpr f32 DRIFT = 0.5f;       // how much the nose leans against the drift
constexpr f32 THRUST_TIME = 0.4f; // s to make up a velocity error at full thrust

// Away from the first rock on the ship's way over the next second or so,
// more the nearer it is; nothing when the way is clear. `way` is where it
// wants to go.
Vec2 AvoidRocks(const Ship& ship, Vec2 way, const Rock* rocks, u32 rockCount)
{
	const f32 speed = Length(ship.velocity);
	const f32 look = 8.0f + 1.2f * speed;
	f32 nearest = look;
	Vec2 push = {};
	for (u32 r = 0; r < rockCount; ++r)
	{
		const Rock& rock = rocks[r];
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

// The velocity the order wants now.
Vec2 Wanted(const Ship& ship, const Order& order, const Mark* target, f32 distance)
{
	const f32 mass = MassOf(ship);
	// What slows it, short of drag (a margin): its reverse thrust.
	const f32 braking = ship.hull.thrustBack / mass;
	switch (order.kind)
	{
		case Order::Kind::Stop: return {};
		case Order::Kind::Move:
		{
			const Vec2 to = order.point - ship.position;
			const f32 left = Length(to);
			if (left < ARRIVED)
				return {};
			// As fast as it can still stop in what is left.
			const f32 speed =
				std::min(ship.hull.maxSpeed, std::sqrt(2.0f * braking * (left - ARRIVED)));
			return to * (speed / left);
		}
		case Order::Kind::Attack:
		{
			if (!target)
				return {};
			const Vec2 to = target->position - ship.position;
			const f32 apart = std::max(Length(to), 1e-3f);
			const Vec2 toward = to * (1.0f / apart);
			if (apart > 1.3f * distance)
				return toward * ship.hull.maxSpeed + target->velocity;
			// Around it, counter-clockwise, drawn in or pushed out to the
			// distance.
			const Vec2 around = {-toward.y, toward.x};
			const f32 off = apart - distance;
			return around * (ORBIT_SPEED * ship.hull.maxSpeed) + toward * (0.8f * off) +
			       target->velocity;
		}
	}
	return {};
}
} // namespace

f32 AttackDistance(const Ship& ship, f32 share)
{
	const f32 reach = WeaponRange(ship);
	return reach > 0.0f ? share * reach : 40.0f;
}

ShipControls Steer(const Ship& ship, const Order& order, const Mark* target, f32 distance,
                   const Rock* rocks, u32 rockCount, Vec2 push)
{
	if (!IsAlive(ship))
		return {};
	Vec2 wanted = Wanted(ship, order, target, distance) + push;
	const f32 want = Length(wanted);
	if (want > 1.0f)
		wanted = wanted + AvoidRocks(ship, wanted * (1.0f / want), rocks, rockCount) * want;
	const f32 mass = MassOf(ship);
	const f32 turnRate = ship.hull.thrustTurn / mass;
	const f32 speed = Length(ship.velocity);
	ShipControls controls;
	// Nearly still and wanting to be: let drag finish it.
	if (Length(wanted) < 1.0f && speed < 1.5f)
		return controls;
	// The nose goes where the push is needed: the wanted velocity, leaning
	// against the drift from it.
	const Vec2 error = wanted - ship.velocity;
	Vec2 heading = Length(wanted) >= 1.0f ? wanted + error * DRIFT : ship.velocity;
	f32 thrust = 0.0f;
	if (Length(wanted) < 1.0f)
	{
		// Stopping: nose along the drift, and reverse thrust against it.
		const f32 off = WrapAngle(AngleOf(ship.velocity) - ship.angle);
		thrust = std::fabs(off) < 0.6f ? -1.0f : 0.0f;
	}
	else
	{
		const Vec2 way = wanted * (1.0f / Length(wanted));
		const f32 along = Dot(ship.velocity, way);
		const f32 off = WrapAngle(AngleOf(heading) - ship.angle);
		if (along > Length(wanted) + 3.0f)
			thrust = std::fabs(off) < 0.8f ? -1.0f : 0.0f; // too fast toward it
		else if (std::fabs(off) < 1.0f)
			thrust =
				std::clamp(Length(error) / (ship.hull.thrust / mass * THRUST_TIME), 0.0f, 1.0f) *
				std::cos(off);
	}
	if (Dot(heading, heading) < 1e-6f)
		heading = Forward(ship.angle);
	const f32 off = WrapAngle(AngleOf(heading) - ship.angle);
	// Turrets aim; the nose only needs to come round smoothly.
	controls.turn = std::clamp(off / std::max(turnRate * TURN_TIME, 1e-4f), -1.0f, 1.0f);
	controls.thrust = std::clamp(thrust, -1.0f, 1.0f);
	return controls;
}
} // namespace sn::bots
