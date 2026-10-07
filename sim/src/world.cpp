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

u32 NextIndex(u32& state, u32 count)
{
	state = state * 1664525u + 1013904223u;
	return (state >> 8) % count;
}

// The point of a circle's edge toward `toward`.
Vec2 Edge(Vec2 center, f32 radius, Vec2 toward)
{
	const Vec2 away = toward - center;
	const f32 length = Length(away);
	return length > 1e-4f ? center + away * (radius / length) : center;
}

f32 DistanceSquared(Vec2 a, Vec2 b) { return Dot(a - b, a - b); }

bool Works(const Module& module) { return module.health > 0.0f && !module.offline; }

// Timers count down by ticks; what float steps leave over is no wait.
constexpr f32 DUE = 1e-4f;

// What the working modules draw from the reactor, MW.
f32 PassiveLoad(const Ship& ship)
{
	f32 load = 0.0f;
	for (u32 i = 0; i < ship.moduleCount; ++i)
	{
		if (Works(ship.modules[i]))
			load += ship.modules[i].type.power;
	}
	return load;
}

// The capacitor charges by what the reactor has left (MW are MJ a second).
// While it is empty and the modules draw more than the reactor gives, they
// go offline from the last one; they come back once it has charged again.
void Charge(Ship& ship, const CombatRules& rules)
{
	const f32 load = PassiveLoad(ship);
	ship.capacitor = std::clamp(ship.capacitor + (ship.hull.reactor - load) * 0.001f * TICK_SECONDS,
	                            0.0f, ship.hull.capacitor);
	if (ship.capacitor <= 0.0f && load > ship.hull.reactor)
	{
		for (u32 i = ship.moduleCount; i-- > 0;)
		{
			Module& module = ship.modules[i];
			if (Works(module) && module.type.power > 0.0f)
			{
				module.offline = true;
				module.working = false;
				return;
			}
		}
	}
	else if (ship.capacitor >= rules.restartCapacitor * ship.hull.capacitor)
	{
		for (u32 i = 0; i < ship.moduleCount; ++i)
		{
			if (ship.modules[i].offline)
			{
				ship.modules[i].offline = false;
				return;
			}
		}
	}
}

// Energy from the capacitor: all of it, or none.
bool Spend(Ship& ship, f32 energy)
{
	if (ship.capacitor < energy)
		return false;
	ship.capacitor -= energy;
	return true;
}

// A magazine filled from the hold, as far as the hold has charges.
void Load(Ship& ship, Module& module)
{
	const u32 count = std::min(module.type.magazine, ship.ammo);
	ship.ammo -= count;
	module.loaded += count;
	ship.cargoUsed = std::max(0.0f, ship.cargoUsed - f32(count) * ship.ammoVolume);
	ship.cargoMass = std::max(0.0f, ship.cargoMass - f32(count) * ship.ammoMass);
}

// A live ship of another team within `reach` of `ship` (to its edge).
const Ship* EnemyWithin(const World& world, const Ship& ship, ShipHandle handle, f32 reach)
{
	const Ship* other = GetShip(world, handle);
	if (!other || !IsAlive(*other) || other->team == ship.team)
		return nullptr;
	const f32 within = reach + other->hull.radius;
	return DistanceSquared(other->position, ship.position) <= within * within ? other : nullptr;
}

// A turret's target: the ship's chosen one while it is within reach (and
// the scanner's), else the nearest enemy that is.
ShipHandle PickTarget(const World& world, const Ship& ship, f32 range, const u32* live,
                      u32 liveCount)
{
	const f32 reach = std::min(range, ship.hull.scanner);
	if (EnemyWithin(world, ship, ship.target, reach))
		return ship.target;
	ShipHandle nearest;
	f32 best = 1e30f;
	for (u32 i = 0; i < liveCount; ++i)
	{
		const Ship& other = world.ships[live[i]];
		if (!IsAlive(other) || other.team == ship.team)
			continue;
		const f32 within = reach + other.hull.radius;
		const f32 distance = DistanceSquared(other.position, ship.position);
		if (distance <= within * within && distance < best)
		{
			best = distance;
			nearest = world.shipIds[live[i]];
		}
	}
	return nearest;
}

// Turns a turret toward `aim` (an angle in the plane) at its speed; returns
// how far off it still is.
f32 TurnTurret(const Ship& ship, Module& module, f32 aim)
{
	const f32 off = WrapAngle(aim - ship.angle - module.angle);
	const f32 step = module.type.turnSpeed * TICK_SECONDS;
	module.angle = WrapAngle(module.angle + std::clamp(off, -step, step));
	return std::fabs(off) - std::min(std::fabs(off), step);
}

// The first rock on the line from `from` to `to`: how far along (0 to 1), or
// above 1 for none.
f32 FirstRock(const World& world, Vec2 from, Vec2 to, u32& rock)
{
	f32 first = 2.0f;
	rock = NO_INDEX;
	for (u32 r = 0; r < world.rockCount; ++r)
	{
		const Rock& candidate = world.rocks[r];
		if (candidate.health <= 0.0f)
			continue;
		const f32 t = SweepContact(from, to - from, candidate.position, candidate.radius);
		if (t >= 0.0f && t < first)
		{
			first = t;
			rock = r;
		}
	}
	return first;
}

void HurtRock(World& world, u32 index, f32 damage, ShipHandle by, Vec2 at, bool tell)
{
	Rock& rock = world.rocks[index];
	rock.health = std::max(0.0f, rock.health - damage);
	if (tell)
		AddEvent(world,
		         {EventType::RockHit, by, {}, index, Edge(rock.position, rock.radius, at), damage});
	if (rock.health <= 0.0f)
		AddEvent(world, {EventType::RockBroken, by, {}, index, rock.position, rock.radius});
}

// One of the ship's external modules (at random) takes part of a hull hit.
void HurtModule(World& world, u32 slot, f32 damage, ShipHandle by)
{
	Ship& ship = world.ships[slot];
	u32 candidates[MAX_MODULES];
	u32 count = 0;
	for (u32 i = 0; i < ship.moduleCount; ++i)
	{
		if (ship.modules[i].type.external && ship.modules[i].health > 0.0f)
			candidates[count++] = i;
	}
	if (count == 0 || damage <= 0.0f)
		return;
	const u32 index = candidates[NextIndex(world.random, count)];
	Module& module = ship.modules[index];
	module.health = std::max(0.0f, module.health - damage);
	if (module.health > 0.0f)
		return;
	module.working = false;
	module.target = {};
	AddEvent(world, {EventType::ModuleBroken, world.shipIds[slot], by, index, ship.position, 0.0f});
}

// Damage to a ship: split between its shield and hull by type; without
// health, the ship is a wreck. A beam's hits (`tell` false) tell no events
// but the end.
void Hurt(World& world, u32 slot, const Damage& damage, ShipHandle by, Vec2 at, bool tell)
{
	Ship& ship = world.ships[slot];
	const ShipHandle handle = world.shipIds[slot];
	const Vec2 edge = Edge(ship.position, ship.hull.radius, at);
	const DamageSplit split = SplitDamage(ship.hull, ship.shield, damage);
	ship.shield = std::max(0.0f, ship.shield - split.shield);
	if (tell && split.shield > 0.0f)
		AddEvent(world, {EventType::ShieldHit, handle, by, NO_INDEX, edge, split.shield});
	if (split.hull <= 0.0f)
		return;
	ship.health = std::max(0.0f, ship.health - split.hull);
	if (tell)
		AddEvent(world, {EventType::HullHit, handle, by, NO_INDEX, edge, split.hull});
	HurtModule(world, slot, split.hull * world.rules.moduleDamage, by);
	if (IsAlive(ship))
		return;
	ship.velocity = {};
	ship.controls = {};
	ship.shield = 0.0f;
	for (u32 i = 0; i < ship.moduleCount; ++i)
		ship.modules[i].working = false;
	AddEvent(world,
	         {EventType::ShipDestroyed, handle, by, NO_INDEX, ship.position, ship.hull.radius});
}

// A laser: on its target, it hurts the first thing on its line each tick,
// while the capacitor pays.
void RunBeam(World& world, u32 slot, Module& module, const u32* live, u32 liveCount)
{
	Ship& ship = world.ships[slot];
	module.target = PickTarget(world, ship, module.type.range, live, liveCount);
	const Ship* target = GetShip(world, module.target);
	if (!target)
		return;
	const f32 off = TurnTurret(ship, module, AngleOf(target->position - ship.position));
	if (off > world.rules.beamAim || !Spend(ship, module.type.energy * TICK_SECONDS))
		return;
	module.working = true;
	Damage tick = module.type.damage;
	for (f32& amount : tick.amount)
		amount *= TICK_SECONDS;
	const Vec2 from = ship.position;
	const f32 length = Length(target->position - from);
	const f32 reach =
		length > 1e-3f ? std::max(0.0f, (length - target->hull.radius) / length) : 0.0f;
	u32 rock = NO_INDEX;
	if (FirstRock(world, from, target->position, rock) < reach)
	{
		HurtRock(world, rock, tick.Total(), world.shipIds[slot], target->position, false);
		return;
	}
	const ShipHandle handle = module.target;
	Hurt(world, Handles::Slot(handle), tick, world.shipIds[slot], from, false);
}

// The ship's shot, into the first free shot of the pool (a full pool loses
// it).
void AddShot(World& world, u32 slot, const Shot& fired)
{
	for (u32 i = 0; i < MAX_SHOTS; ++i)
	{
		const u32 index = (world.nextShot + i) % MAX_SHOTS;
		Shot& shot = world.shots[index];
		if (shot.life > 0.0f)
			continue;
		shot = fired;
		shot.owner = world.shipIds[slot];
		world.nextShot = (index + 1) % MAX_SHOTS;
		AddEvent(world, {EventType::Fired, shot.owner, {}, NO_INDEX, shot.position, 0.0f});
		return;
	}
}

// A gun: it reloads from the hold when its magazine is empty, leads its
// target, and fires once on the lead.
void RunGun(World& world, u32 slot, Module& module, const u32* live, u32 liveCount)
{
	Ship& ship = world.ships[slot];
	module.cooldown = std::max(0.0f, module.cooldown - TICK_SECONDS);
	if (module.reloading > 0.0f)
	{
		module.reloading -= TICK_SECONDS;
		if (module.reloading <= DUE)
		{
			module.reloading = 0.0f;
			Load(ship, module);
		}
	}
	else if (module.loaded == 0 && ship.ammo > 0)
		module.reloading = module.type.reload;
	module.target = PickTarget(world, ship, module.type.range, live, liveCount);
	const Ship* target = GetShip(world, module.target);
	if (!target)
		return;
	const Vec2 lead = Lead(ship.position, ship.velocity, module.type.shotSpeed, target->position,
	                       target->velocity);
	const f32 off = TurnTurret(ship, module, AngleOf(lead - ship.position));
	if (off > world.rules.shotAim || module.cooldown > DUE || module.loaded == 0 ||
	    !Spend(ship, module.type.energy))
		return;
	const Vec2 direction = Forward(ship.angle + module.angle);
	Shot shot;
	shot.position = ship.position + direction * (ship.hull.radius + module.type.shotRadius);
	shot.velocity = ship.velocity + direction * module.type.shotSpeed;
	shot.life = module.type.range / std::max(module.type.shotSpeed, 1.0f);
	shot.radius = module.type.shotRadius;
	shot.damage = module.type.damage;
	shot.team = ship.team;
	--module.loaded;
	module.cooldown = module.type.interval;
	AddShot(world, slot, shot);
}

// A booster: shield back for energy, while the shield is down and the
// capacitor keeps its reserve.
void RunBooster(Ship& ship, Module& module, const CombatRules& rules)
{
	const f32 energy = module.type.energy * TICK_SECONDS;
	if (ship.shield >= ship.hull.shield ||
	    ship.capacitor - energy < rules.boosterCapacitor * ship.hull.capacitor ||
	    !Spend(ship, energy))
		return;
	module.working = true;
	ship.shield = std::min(ship.hull.shield, ship.shield + module.type.boost * TICK_SECONDS);
}

// A ship's capacitor, modules and shield for one tick.
void RunSystems(World& world, u32 slot, const u32* live, u32 liveCount)
{
	Ship& ship = world.ships[slot];
	Charge(ship, world.rules);
	for (u32 i = 0; i < ship.moduleCount && IsAlive(ship); ++i)
	{
		Module& module = ship.modules[i];
		module.working = false;
		if (!Works(module))
		{
			module.target = {};
			continue;
		}
		switch (module.type.kind)
		{
			case ModuleKind::Laser: RunBeam(world, slot, module, live, liveCount); break;
			case ModuleKind::Plasma: RunGun(world, slot, module, live, liveCount); break;
			case ModuleKind::ShieldBooster: RunBooster(ship, module, world.rules); break;
			case ModuleKind::CapacitorBattery:
			case ModuleKind::None: break;
		}
	}
	ship.shield = std::min(ship.hull.shield, ship.shield + ship.hull.shieldRegen * TICK_SECONDS);
}

// Moves a shot. The first rock, or live ship of another team, on its way
// takes its damage, and the shot ends there. `ships` are the live ships'
// slots.
void FlyShot(World& world, Shot& shot, const u32* ships, u32 shipCount)
{
	const Vec2 path = shot.velocity * TICK_SECONDS;
	f32 first = 2.0f; // beyond the way: nothing hit
	u32 rock = NO_INDEX;
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
			rock = NO_INDEX;
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
		Hurt(world, target, shot.damage, shot.owner, at, true);
	else
		HurtRock(world, rock, shot.damage.Total(), shot.owner, at, true);
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
	AddEvent(world, {EventType::ShipBumped, world.shipIds[a], world.shipIds[b], NO_INDEX, contact,
	                 closing});
	AddEvent(world, {EventType::ShipBumped, world.shipIds[b], world.shipIds[a], NO_INDEX, contact,
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

// Crates age; a players' ship within reach with room in its hold takes one.
void PickCrates(World& world, const u32* live, u32 liveCount)
{
	for (u32 c = 0; c < MAX_CRATES; ++c)
	{
		Crate& crate = world.crates[c];
		if (crate.life <= 0.0f)
			continue;
		crate.life -= TICK_SECONDS;
		for (u32 i = 0; i < liveCount; ++i)
		{
			Ship& ship = world.ships[live[i]];
			const f32 reach = world.rules.pickupRange + ship.hull.radius;
			if (!IsAlive(ship) || ship.team != PLAYERS ||
			    DistanceSquared(ship.position, crate.position) > reach * reach ||
			    ship.hull.cargo - ship.cargoUsed < crate.volume - 1e-4f)
				continue;
			ship.cargoUsed += crate.volume;
			ship.cargoMass += crate.mass;
			AddEvent(world,
			         {EventType::CratePicked, world.shipIds[live[i]], {}, c, crate.position, 0.0f});
			crate = {};
			break;
		}
		if (crate.life <= 0.0f && crate.volume > 0.0f)
		{
			AddEvent(world, {EventType::CrateLost, {}, {}, c, crate.position, 0.0f});
			crate = {};
		}
	}
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

f32 MassOf(const Ship& ship)
{
	return std::max(1.0f, ship.hull.mass + ship.cargoMass * ship.hull.cargoMassFactor);
}

f32 WeaponRange(const Ship& ship)
{
	f32 range = 0.0f;
	for (u32 i = 0; i < ship.moduleCount; ++i)
	{
		const Module& module = ship.modules[i];
		if (IsWeapon(module.type.kind) && module.health > 0.0f)
			range = std::max(range, module.type.range);
	}
	return range;
}

DamageSplit SplitDamage(const HullClass& hull, f32 shield, const Damage& damage)
{
	f32 taken[DAMAGE_TYPES];
	for (u32 t = 0; t < DAMAGE_TYPES; ++t)
		taken[t] = std::max(0.0f, damage.amount[t]) * std::clamp(1.0f - hull.resist[t], 0.0f, 1.0f);
	const f32 soft = taken[u32(DamageType::Em)] + taken[u32(DamageType::Explosive)];
	const f32 hard = taken[u32(DamageType::Kinetic)] + taken[u32(DamageType::Thermal)];
	shield = std::max(0.0f, shield);
	DamageSplit split;
	split.shield = std::min(shield, soft);
	split.hull = soft - split.shield;
	const f32 stopped =
		std::min(shield - split.shield, std::min(hard, std::max(0.0f, hull.shieldThreshold)));
	split.shield += stopped;
	split.hull += hard - stopped;
	return split;
}

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

void AddEvent(World& world, const Event& event)
{
	if (world.eventCount < MAX_EVENTS)
		world.events[world.eventCount++] = event;
}

u32 DropCrate(World& world, Vec2 position, f32 volume, f32 mass, f32 life)
{
	for (u32 c = 0; c < MAX_CRATES; ++c)
	{
		Crate& crate = world.crates[c];
		if (crate.life > 0.0f)
			continue;
		crate = {position, life, std::max(volume, 1e-3f), std::max(mass, 0.0f)};
		AddEvent(world, {EventType::CrateDropped, {}, {}, c, position, crate.volume});
		return c;
	}
	return NO_INDEX;
}

void FlyShip(Ship& ship)
{
	const HullClass& hull = ship.hull;
	const f32 mass = MassOf(ship);
	const f32 turn = std::clamp(ship.controls.turn, -1.0f, 1.0f);
	const f32 thrust = std::clamp(ship.controls.thrust, -1.0f, 1.0f);
	// Kept in [-pi, pi], so that it stays precise over a long battle.
	ship.angle = WrapAngle(ship.angle + turn * (hull.thrustTurn / mass) * TICK_SECONDS);

	const f32 push = thrust > 0.0f ? thrust * hull.thrust : thrust * hull.thrustBack;
	ship.velocity = ship.velocity + Forward(ship.angle) * (push / mass * TICK_SECONDS);
	// Arcade space: speed fades without thrust, and has a limit.
	ship.velocity = ship.velocity * std::max(0.0f, 1.0f - hull.drag * TICK_SECONDS);
	const f32 speed = Length(ship.velocity);
	if (speed > hull.maxSpeed)
		ship.velocity = ship.velocity * (hull.maxSpeed / speed);
	ship.position = ship.position + ship.velocity * TICK_SECONDS;
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
	Ship& spawned = world.ships[slot];
	spawned = ship;
	if (spawned.health <= 0.0f)
	{
		spawned.health = ship.hull.health;
		spawned.shield = ship.hull.shield;
		spawned.capacitor = ship.hull.capacitor;
	}
	spawned.moduleCount = std::min(spawned.moduleCount, MAX_MODULES);
	for (u32 i = 0; i < spawned.moduleCount; ++i)
	{
		Module& module = spawned.modules[i];
		if (module.type.kind == ModuleKind::Plasma && module.loaded == 0)
			Load(spawned, module);
	}
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
	ship->health = ship->hull.health;
	ship->shield = ship->hull.shield;
	ship->capacitor = ship->hull.capacitor;
	ship->controls = {};
	for (u32 i = 0; i < ship->moduleCount; ++i)
	{
		Module& module = ship->modules[i];
		module.health = module.type.health;
		module.offline = false;
		module.cooldown = 0.0f;
		module.angle = 0.0f;
		if (module.type.kind == ModuleKind::Plasma && module.loaded == 0 &&
		    module.reloading <= 0.0f)
			Load(*ship, module);
	}
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

void SetTarget(World& world, ShipHandle ship, ShipHandle target)
{
	if (Ship* found = GetShip(world, ship))
		found->target = target;
}

f32 RockHealth(f32 radius) { return std::max(1.0f, std::ceil(3.5f * radius * radius)); }

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
		FlyShip(world.ships[live[i]]);
	// Ships a beam destroyed earlier in the tick do no more.
	for (u32 i = 0; i < liveCount; ++i)
	{
		if (IsAlive(world.ships[live[i]]))
			RunSystems(world, live[i], live, liveCount);
	}
	for (Shot& shot : world.shots)
	{
		if (shot.life > 0.0f)
			FlyShot(world, shot, live, liveCount);
	}
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
	PickCrates(world, live, liveCount);
	++world.tick;
}
} // namespace sn::sim
