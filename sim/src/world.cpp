#include <sn/sim/world.h>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sn::sim
{
namespace
{
using Handles = ph::HandleAllocator<ShipTag, MAX_SHIPS>;

// A small, steady generator: the same seed makes the same field.
struct Random
{
	u32 state;

	f32 Next()
	{
		state = state * 1664525u + 1013904223u;
		return f32(state >> 8) / f32(1u << 24);
	}
};

// The shield comes back once the ship has gone without hits for a while.
void Rest(Ship& ship)
{
	ship.rested += TICK_SECONDS;
	if (ship.rested >= ship.hull.shieldDelay)
		ship.shield =
			std::min(ship.hull.shield, ship.shield + ship.hull.shieldRegen * TICK_SECONDS);
}

// The ship's gun, into the first free shot of the pool (a full pool loses
// the shot).
void Fire(World& world, u32 slot)
{
	Shot fired;
	if (!FireGun(world.ships[slot], fired))
		return;
	for (u32 i = 0; i < MAX_SHOTS; ++i)
	{
		const u32 index = (world.nextShot + i) % MAX_SHOTS;
		Shot& shot = world.shots[index];
		if (shot.life > 0.0f)
			continue;
		fired.owner = world.shipIds[slot];
		shot = fired;
		world.nextShot = (index + 1) % MAX_SHOTS;
		AddEvent(world, {EventType::Fired, shot.owner, {}, NO_ROCK, shot.position, 0.0f});
		return;
	}
}

// The point of a circle's edge toward `toward`.
Vec2 Edge(Vec2 center, f32 radius, Vec2 toward)
{
	const Vec2 away = toward - center;
	const f32 length = Length(away);
	return length > 1e-4f ? center + away * (radius / length) : center;
}

// A shot's damage to a ship: the shield takes what it can, the hull the
// rest; without health, the ship is a wreck.
void Damage(World& world, u32 slot, const Shot& shot, Vec2 at)
{
	Ship& ship = world.ships[slot];
	const ShipHandle handle = world.shipIds[slot];
	ship.rested = 0.0f;
	const Vec2 edge = Edge(ship.position, ship.hull.radius, at);
	const f32 absorbed = std::min(ship.shield, shot.damage);
	const f32 left = shot.damage - absorbed;
	if (absorbed > 0.0f)
	{
		ship.shield -= absorbed;
		AddEvent(world, {EventType::ShieldHit, handle, shot.owner, NO_ROCK, edge, absorbed});
	}
	if (left <= 0.0f)
		return;
	ship.health = std::max(0.0f, ship.health - left);
	AddEvent(world, {EventType::HullHit, handle, shot.owner, NO_ROCK, edge, left});
	if (IsAlive(ship))
		return;
	ship.velocity = {};
	ship.controls = {};
	ship.shield = 0.0f;
	AddEvent(world, {EventType::ShipDestroyed, handle, shot.owner, NO_ROCK, ship.position,
	                 ship.hull.radius});
}

// Moves a shot. The first rock, or live ship of another team, on its way
// takes its damage, and the shot ends there. `ships` are the live ships'
// slots.
void FlyShot(World& world, Shot& shot, const u32* ships, u32 shipCount)
{
	const Vec2 path = shot.velocity * TICK_SECONDS;
	f32 first = 2.0f; // beyond the way: nothing hit
	u32 rock = NO_ROCK;
	u32 target = MAX_SHIPS;
	for (u32 r = 0; r < world.rockCount; ++r)
	{
		const Rock& candidate = world.rocks[r];
		if (candidate.health <= 0.0f)
			continue;
		const f32 t =
			SweepContact(shot.position, path, candidate.position, candidate.radius + shot.radius);
		if (t >= 0.0f && t < first)
		{
			first = t;
			rock = r;
		}
	}
	for (u32 i = 0; i < shipCount; ++i)
	{
		const Ship& ship = world.ships[ships[i]];
		if (!IsAlive(ship) || ship.team == shot.team)
			continue;
		const f32 t =
			SweepContact(shot.position, path, ship.position, ship.hull.radius + shot.radius);
		if (t >= 0.0f && t < first)
		{
			first = t;
			rock = NO_ROCK;
			target = ships[i];
		}
	}
	shot.life -= TICK_SECONDS;
	if (first > 1.0f)
	{
		shot.position = shot.position + path;
		if (shot.life <= 0.0f)
			shot = {};
		return;
	}
	const Vec2 at = shot.position + path * first;
	if (target < MAX_SHIPS)
		Damage(world, target, shot, at);
	else
	{
		Rock& hit = world.rocks[rock];
		hit.health = std::max(0.0f, hit.health - shot.damage);
		AddEvent(world, {EventType::RockHit,
		                 shot.owner,
		                 {},
		                 rock,
		                 Edge(hit.position, hit.radius, at),
		                 shot.damage});
		if (hit.health <= 0.0f)
			AddEvent(world,
			         {EventType::RockBroken, shot.owner, {}, rock, hit.position, hit.radius});
	}
	shot = {};
}

// Two ships that overlap are pushed apart, half the way each, and their
// speed toward each other turns back, partly: equal masses.
void CollideShips(World& world, u32 a, u32 b)
{
	Ship& first = world.ships[a];
	Ship& second = world.ships[b];
	const Vec2 away = first.position - second.position;
	const f32 reach = first.hull.radius + second.hull.radius;
	if (Dot(away, away) >= reach * reach)
		return;
	const f32 distance = Length(away);
	const Vec2 normal = distance > 1e-4f ? away * (1.0f / distance) : Vec2{1.0f, 0.0f};
	const f32 overlap = reach - distance;
	first.position = first.position + normal * (0.5f * overlap);
	second.position = second.position - normal * (0.5f * overlap);
	const f32 closing = Dot(second.velocity - first.velocity, normal);
	if (closing <= 0.0f)
		return;
	const f32 bounce = 0.5f * (first.hull.bounce + second.hull.bounce);
	const f32 push = 0.5f * closing * (1.0f + bounce);
	first.velocity = first.velocity + normal * push;
	second.velocity = second.velocity - normal * push;
	const Vec2 contact = second.position + normal * second.hull.radius;
	AddEvent(world, {EventType::ShipBumped, world.shipIds[a], world.shipIds[b], NO_ROCK, contact,
	                 closing});
	AddEvent(world, {EventType::ShipBumped, world.shipIds[b], world.shipIds[a], NO_ROCK, contact,
	                 closing});
}

// Rocks do not move: the ship bounces off them (BounceOffRocks), and each
// bump is an event.
void CollideWithRocks(World& world, u32 slot)
{
	RockBump bumps[8];
	const u32 count = BounceOffRocks(world.ships[slot], world.rocks, world.rockCount, bumps, 8);
	for (u32 i = 0; i < std::min(count, 8u); ++i)
		AddEvent(world, {EventType::ShipBumped,
		                 world.shipIds[slot],
		                 {},
		                 bumps[i].rock,
		                 bumps[i].at,
		                 bumps[i].into});
}
} // namespace

Vec2 Forward(f32 angle) { return {-std::sin(angle), std::cos(angle)}; }

f32 AngleOf(Vec2 direction) { return std::atan2(-direction.x, direction.y); }

f32 WrapAngle(f32 angle)
{
	if (angle > ph::PI || angle < -ph::PI)
		angle = std::remainder(angle, 2.0f * ph::PI);
	return angle;
}

f32 Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

f32 Length(Vec2 v) { return std::sqrt(Dot(v, v)); }

void AddEvent(World& world, const Event& event)
{
	if (world.eventCount < MAX_EVENTS)
		world.events[world.eventCount++] = event;
}

void FlyShip(Ship& ship)
{
	const HullClass& hull = ship.hull;
	const f32 turn = std::clamp(ship.controls.turn, -1.0f, 1.0f);
	const f32 thrust = std::clamp(ship.controls.thrust, -1.0f, 1.0f);
	// Kept in [-pi, pi], so that it stays precise over a long battle.
	ship.angle = WrapAngle(ship.angle + turn * hull.turnRate * TICK_SECONDS);

	const f32 push = thrust > 0.0f ? thrust : thrust * hull.reverse;
	ship.velocity = ship.velocity + Forward(ship.angle) * (push * hull.acceleration * TICK_SECONDS);
	// Arcade space: speed fades without thrust, and has a limit.
	ship.velocity = ship.velocity * std::max(0.0f, 1.0f - hull.drag * TICK_SECONDS);
	const f32 speed = Length(ship.velocity);
	if (speed > hull.maxSpeed)
		ship.velocity = ship.velocity * (hull.maxSpeed / speed);
	ship.position = ship.position + ship.velocity * TICK_SECONDS;
}

bool FireGun(Ship& ship, Shot& shot)
{
	ship.cooldown = std::max(0.0f, ship.cooldown - TICK_SECONDS);
	if (!ship.controls.fire || ship.cooldown > 0.0f)
		return false;
	const WeaponClass& weapon = ship.weapon;
	const Vec2 forward = Forward(ship.angle);
	shot = {};
	shot.position = ship.position + forward * (ship.hull.radius + weapon.radius);
	shot.velocity = ship.velocity + forward * weapon.speed;
	shot.life = weapon.life;
	shot.radius = weapon.radius;
	shot.damage = weapon.damage;
	shot.team = ship.team;
	ship.cooldown = weapon.interval;
	return true;
}

u32 BounceOffRocks(Ship& ship, const Rock* rocks, u32 rockCount, RockBump* bumps, u32 capacity)
{
	u32 count = 0;
	for (u32 r = 0; r < rockCount; ++r)
	{
		const Rock& rock = rocks[r];
		if (rock.health <= 0.0f)
			continue;
		const Vec2 away = ship.position - rock.position;
		const f32 reach = ship.hull.radius + rock.radius;
		if (Dot(away, away) >= reach * reach)
			continue;
		const f32 distance = Length(away);
		const Vec2 normal = distance > 1e-4f ? away * (1.0f / distance) : Vec2{0.0f, 1.0f};
		ship.position = rock.position + normal * reach;
		const f32 into = -Dot(ship.velocity, normal);
		if (into <= 0.0f)
			continue;
		ship.velocity = ship.velocity + normal * (into * (1.0f + ship.hull.bounce));
		if (bumps && count < capacity)
			bumps[count] = {r, rock.position + normal * rock.radius, into};
		++count;
	}
	return count;
}

// A shot crosses up to 4 m a tick, more than the smallest rocks: a sweep
// cannot skip them.
f32 SweepContact(Vec2 from, Vec2 path, Vec2 center, f32 reach)
{
	const Vec2 off = from - center;
	const f32 c = Dot(off, off) - reach * reach;
	if (c <= 0.0f)
		return 0.0f; // within reach already
	const f32 a = Dot(path, path);
	const f32 b = Dot(off, path);
	if (a <= 0.0f || b >= 0.0f)
		return -1.0f; // still, or going away
	const f32 discriminant = b * b - a * c;
	if (discriminant < 0.0f)
		return -1.0f;
	const f32 t = (-b - std::sqrt(discriminant)) / a;
	return t <= 1.0f ? t : -1.0f;
}

ShipHandle SpawnShip(World& world, const Ship& ship)
{
	const ShipHandle handle = world.shipHandles.Allocate();
	if (!handle)
		return {};
	const u32 slot = Handles::Slot(handle);
	world.ships[slot] = ship;
	world.ships[slot].health = ship.hull.health;
	world.ships[slot].shield = ship.hull.shield;
	world.ships[slot].rested = ship.hull.shieldDelay;
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

void ReviveShip(World& world, ShipHandle handle, Vec2 position, f32 angle)
{
	Ship* ship = GetShip(world, handle);
	if (!ship)
		return;
	ship->position = position;
	ship->velocity = {};
	ship->angle = WrapAngle(angle);
	ship->cooldown = 0.0f;
	ship->health = ship->hull.health;
	ship->shield = ship->hull.shield;
	ship->rested = ship->hull.shieldDelay;
	ship->controls = {};
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

f32 RockHealth(f32 radius) { return std::max(1.0f, std::ceil(0.3f * radius * radius)); }

void MakeAsteroidField(World& world, const AsteroidFieldDesc& desc)
{
	world.rockCount = 0;
	std::fill(std::begin(world.rocks), std::end(world.rocks), Rock{});
	Random random{desc.seed};
	const u32 count = std::min(desc.count, MAX_ROCKS);
	// More small rocks than big ones; each needs room of its own, and the
	// field gives up on a rock after a few tries where it is crowded.
	for (u32 tries = 0; world.rockCount < count && tries < count * 30; ++tries)
	{
		const f32 size = random.Next();
		const f32 radius = desc.minRadius + (desc.maxRadius - desc.minRadius) * size * size;
		const Vec2 position = {(random.Next() - 0.5f) * desc.size,
		                       (random.Next() - 0.5f) * desc.size};
		if (Length(position) < desc.clearing + radius)
			continue;
		bool room = true;
		for (u32 r = 0; r < world.rockCount && room; ++r)
		{
			const Vec2 off = position - world.rocks[r].position;
			const f32 apart = radius + world.rocks[r].radius + desc.spacing;
			room = Dot(off, off) >= apart * apart;
		}
		if (room)
			world.rocks[world.rockCount++] = {position, radius, RockHealth(radius)};
	}
}

void Step(World& world)
{
	world.eventCount = 0;
	// The live ships, in slot order; wrecks wait out of play.
	u32 live[MAX_SHIPS];
	u32 liveCount = 0;
	for (u32 slot = 0; slot < MAX_SHIPS; ++slot)
	{
		if (world.shipIds[slot] && IsAlive(world.ships[slot]))
			live[liveCount++] = slot;
	}
	for (u32 i = 0; i < liveCount; ++i)
	{
		FlyShip(world.ships[live[i]]);
		Rest(world.ships[live[i]]);
		Fire(world, live[i]);
	}
	for (Shot& shot : world.shots)
	{
		if (shot.life > 0.0f)
			FlyShot(world, shot, live, liveCount);
	}
	// Ships the shots destroyed bump nothing more.
	for (u32 i = 0; i < liveCount; ++i)
	{
		for (u32 j = i + 1; j < liveCount && IsAlive(world.ships[live[i]]); ++j)
		{
			if (IsAlive(world.ships[live[j]]))
				CollideShips(world, live[i], live[j]);
		}
	}
	for (u32 i = 0; i < liveCount; ++i)
	{
		if (IsAlive(world.ships[live[i]]))
			CollideWithRocks(world, live[i]);
	}
	++world.tick;
}
} // namespace sn::sim
