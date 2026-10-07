#include <sn/sim/catalog.h>
#include <sn/sim/fitting.h>
#include <sn/sim/world.h>

#include <doctest/doctest.h>

#include <cmath>
#include <memory>

using namespace sn::sim;

namespace
{
// Worlds hold every pool inline: too big for a test's stack.
std::unique_ptr<World> MakeWorld() { return std::make_unique<World>(); }

u32 Ticks(f32 seconds) { return u32(seconds * f32(TICK_RATE) + 0.5f); }

void StepFor(World& world, f32 seconds)
{
	for (u32 i = 0; i < Ticks(seconds); ++i)
		Step(world);
}

// A ship of the catalog with these modules (slot order) and hold.
Ship Fitted(std::initializer_list<const char*> modules, std::vector<Stack> hold = {},
            const char* hull = "lancer")
{
	ShipPlan plan;
	plan.hull = hull;
	for (const char* id : modules)
		plan.modules.emplace_back(id);
	plan.hold = std::move(hold);
	Ship ship;
	std::string error;
	const bool made = MakeShip(GetCatalog(), FitFromPlan(GetCatalog(), plan), ship, &error);
	INFO(error);
	CHECK(made);
	return ship;
}

ShipHandle SpawnAt(World& world, Ship ship, Vec2 position, u8 team)
{
	ship.position = position;
	ship.team = team;
	return SpawnShip(world, ship);
}

// The events of the next `seconds`, counted by type.
struct Tally
{
	u32 count[LAST_EVENT_TYPE + 1] = {};

	u32 operator[](EventType type) const { return count[u32(type)]; }
};

Tally StepAndCount(World& world, f32 seconds)
{
	Tally tally;
	for (u32 i = 0; i < Ticks(seconds); ++i)
	{
		Step(world);
		for (u32 e = 0; e < world.eventCount; ++e)
			++tally.count[u32(world.events[e].type)];
	}
	return tally;
}

void AddRock(World& world, Vec2 position, f32 radius, f32 health = 0.0f)
{
	world.rocks[world.rockCount++] = {position, radius,
	                                  health > 0.0f ? health : RockHealth(radius)};
}

Damage Of(DamageType type, f32 amount)
{
	Damage damage;
	damage.amount[u32(type)] = amount;
	return damage;
}
} // namespace

TEST_CASE("sim: a ship thrusts forward, up to its top speed")
{
	auto world = MakeWorld();
	const ShipHandle ship = SpawnShip(*world, {});
	REQUIRE(ship);
	SetControls(*world, ship, {0.0f, 1.0f});
	StepFor(*world, 10.0f);
	const Ship* state = GetShip(*world, ship);
	REQUIRE(state);
	CHECK(world->tick == 10 * TICK_RATE);
	// Facing +y at angle 0: it went up the screen, at its limit.
	CHECK(std::abs(state->position.x) < 0.001f);
	CHECK(state->position.y > 100.0f);
	CHECK(state->velocity.y == doctest::Approx(state->hull.maxSpeed).epsilon(0.01));
}

TEST_CASE("sim: turning, drag and reverse follow thrust and mass")
{
	auto world = MakeWorld();
	const ShipHandle ship = SpawnShip(*world, {});
	const HullClass hull;
	// A quarter turn left: it faces -x.
	SetControls(*world, ship, {1.0f, 0.0f});
	StepFor(*world, 0.5f * ph::PI / (hull.thrustTurn / hull.mass));
	CHECK(GetShip(*world, ship)->angle == doctest::Approx(0.5f * ph::PI).epsilon(0.05));

	// Without thrust, speed fades away.
	GetShip(*world, ship)->velocity = {10.0f, 0.0f};
	SetControls(*world, ship, {});
	StepFor(*world, 10.0f);
	CHECK(std::abs(GetShip(*world, ship)->velocity.x) < 0.1f);

	// Reverse is weaker than forward; a loaded hold is slower than an
	// empty one.
	const auto push = [&](f32 thrust, f32 cargoMass)
	{
		Ship* state = GetShip(*world, ship);
		state->angle = 0.0f;
		state->velocity = {};
		state->cargoMass = cargoMass;
		SetControls(*world, ship, {0.0f, thrust});
		Step(*world);
		return std::abs(GetShip(*world, ship)->velocity.y);
	};
	const f32 forward = push(1.0f, 0.0f);
	CHECK(push(-1.0f, 0.0f) < forward);
	CHECK(push(1.0f, hull.mass) == doctest::Approx(0.5f * forward).epsilon(0.01));
}

TEST_CASE("sim: removed ships leave stale handles, and a world copies")
{
	auto world = MakeWorld();
	const ShipHandle first = SpawnShip(*world, {});
	RemoveShip(*world, first);
	CHECK(!GetShip(*world, first));
	const ShipHandle second = SpawnShip(*world, {});
	CHECK(second.index == first.index); // the slot again, another generation
	CHECK(!GetShip(*world, first));
	REQUIRE(GetShip(*world, second));

	// A copy is a snapshot: stepping the world does not change it.
	SetControls(*world, second, {0.0f, 1.0f});
	auto snapshot = std::make_unique<World>(*world);
	Step(*world);
	CHECK(GetShip(*snapshot, second)->position.y == 0.0f);
	CHECK(GetShip(*world, second)->position.y > 0.0f);
}

TEST_CASE("sim: an asteroid field is the same each time, with room in the middle")
{
	auto world = MakeWorld();
	auto again = MakeWorld();
	AsteroidFieldDesc desc;
	MakeAsteroidField(*world, desc);
	MakeAsteroidField(*again, desc);
	REQUIRE(world->rockCount == desc.count);
	u32 same = 0;
	u32 clear = 0;
	u32 crowded = 0;
	for (u32 r = 0; r < world->rockCount; ++r)
	{
		const Rock& rock = world->rocks[r];
		same += rock.position.x == again->rocks[r].position.x &&
		        rock.radius == again->rocks[r].radius && rock.health == RockHealth(rock.radius);
		clear += std::sqrt(rock.position.x * rock.position.x + rock.position.y * rock.position.y) >=
		         desc.clearing + rock.radius;
		for (u32 other = r + 1; other < world->rockCount; ++other)
		{
			const Vec2 off = rock.position - world->rocks[other].position;
			const f32 apart = rock.radius + world->rocks[other].radius + desc.spacing;
			crowded += off.x * off.x + off.y * off.y < apart * apart - 0.01f;
		}
	}
	CHECK(same == desc.count);
	CHECK(clear == desc.count);
	CHECK(crowded == 0);
	desc.seed = 2;
	MakeAsteroidField(*again, desc);
	CHECK(again->rocks[0].position.x != world->rocks[0].position.x);
}

TEST_CASE("sim: a ship bounces off a rock instead of going through")
{
	auto world = MakeWorld();
	AddRock(*world, {0.0f, 12.0f}, 3.0f);
	Ship ship;
	ship.velocity = {0.0f, 30.0f};
	const ShipHandle handle = SpawnShip(*world, ship);
	const Tally tally = StepAndCount(*world, 1.0f);
	const Ship* state = GetShip(*world, handle);
	CHECK(tally[EventType::ShipBumped] >= 1);
	CHECK(state->velocity.y < 0.0f); // turned back
	// Never inside: on the rock's edge at most.
	CHECK(state->position.y <= 12.0f - 3.0f - state->hull.radius + 0.001f);
}

TEST_CASE("sim: damage splits by type: EM to the shield, kinetic past its threshold")
{
	HullClass hull;
	hull.shieldThreshold = 10.0f;
	for (f32& resist : hull.resist)
		resist = 0.0f;

	// The developer's examples (docs/solo-loop.md).
	DamageSplit split = SplitDamage(hull, 100.0f, Of(DamageType::Kinetic, 30.0f));
	CHECK(split.shield == 10.0f);
	CHECK(split.hull == 20.0f);
	split = SplitDamage(hull, 100.0f, Of(DamageType::Kinetic, 6.0f));
	CHECK(split.shield == 6.0f);
	CHECK(split.hull == 0.0f);
	split = SplitDamage(hull, 100.0f, Of(DamageType::Thermal, 14.0f));
	CHECK(split.shield == 10.0f);
	CHECK(split.hull == 4.0f);
	split = SplitDamage(hull, 100.0f, Of(DamageType::Em, 30.0f));
	CHECK(split.shield == 30.0f);
	CHECK(split.hull == 0.0f);
	split = SplitDamage(hull, 100.0f, Of(DamageType::Explosive, 30.0f));
	CHECK(split.shield == 30.0f);

	// A shield that runs out in the middle of a hit lets the rest through.
	split = SplitDamage(hull, 12.0f, Of(DamageType::Em, 30.0f));
	CHECK(split.shield == 12.0f);
	CHECK(split.hull == 18.0f);
	split = SplitDamage(hull, 4.0f, Of(DamageType::Kinetic, 30.0f));
	CHECK(split.shield == 4.0f);
	CHECK(split.hull == 26.0f);
	split = SplitDamage(hull, 0.0f, Of(DamageType::Em, 30.0f));
	CHECK(split.hull == 30.0f);

	// One threshold for a hit's kinetic and thermal together.
	Damage both = Of(DamageType::Kinetic, 6.0f);
	both.amount[u32(DamageType::Thermal)] = 6.0f;
	split = SplitDamage(hull, 100.0f, both);
	CHECK(split.shield == 10.0f);
	CHECK(split.hull == 2.0f);

	// Resistances first: 20% of a kinetic 30 goes, 24 is left.
	hull.resist[u32(DamageType::Kinetic)] = 0.2f;
	split = SplitDamage(hull, 100.0f, Of(DamageType::Kinetic, 30.0f));
	CHECK(split.shield == 10.0f);
	CHECK(split.hull == doctest::Approx(14.0f));
}

TEST_CASE("sim: the plasma gun turns to its target, leads it, and fires its magazine")
{
	auto world = MakeWorld();
	const ShipHandle shooter =
		SpawnAt(*world, Fitted({"plasma_s"}, {{"plasma_charge_s", 100}}), {}, PLAYERS);
	// Behind the shooter: the turret must come around first.
	const ShipHandle target = SpawnAt(*world, Ship{}, {0.0f, -40.0f}, BOTS);
	const Ship& gunner = *GetShip(*world, shooter);
	const Module& gun = gunner.modules[0];
	REQUIRE(gun.type.kind == ModuleKind::Plasma);
	const u32 magazine = gun.type.magazine;
	CHECK(magazine == 30);
	CHECK(gun.loaded == magazine); // loaded from the hold at launch
	CHECK(gunner.ammo == 100 - magazine);

	// Half a turn at 3 rad/s takes about a second: nothing fires before.
	const Tally turning = StepAndCount(*world, 0.8f);
	CHECK(turning[EventType::Fired] == 0);
	CHECK(std::abs(gun.angle) == doctest::Approx(0.8f * gun.type.turnSpeed).epsilon(0.05));
	// Then 3 shots a second, every one a hit on a still target.
	const Tally firing = StepAndCount(*world, 2.2f);
	CHECK(firing[EventType::Fired] >= 5);
	CHECK(firing[EventType::Fired] <= 7);
	const Tally landing = StepAndCount(*world, 0.6f);
	CHECK(firing[EventType::ShieldHit] + landing[EventType::ShieldHit] >=
	      firing[EventType::Fired] - 1);
	CHECK(GetShip(*world, target)->shield < HullClass{}.shield);
	CHECK(gun.target == target);
}

TEST_CASE("sim: an empty magazine reloads from the hold; an empty hold stops the gun")
{
	auto world = MakeWorld();
	const ShipHandle shooter =
		SpawnAt(*world, Fitted({"plasma_s"}, {{"plasma_charge_s", 40}}), {}, PLAYERS);
	Ship target;
	target.hull.health = 1e6f;
	target.health = 1e6f;
	SpawnAt(*world, target, {0.0f, 30.0f}, BOTS);
	const Ship& ship = *GetShip(*world, shooter);
	const f32 cargoBefore = ship.cargoUsed;
	CHECK(cargoBefore == doctest::Approx(0.1f)); // 10 charges left in the hold
	const Module& gun = ship.modules[0];
	// 30 shots at 3 a second, then a reload of 4 s, then the last 10.
	Tally tally = StepAndCount(*world, 10.5f);
	CHECK(tally[EventType::Fired] == 30);
	CHECK(gun.reloading > 0.0f);
	tally = StepAndCount(*world, 4.0f + 4.0f);
	CHECK(tally[EventType::Fired] == 10);
	CHECK(ship.ammo == 0);
	CHECK(ship.cargoUsed == doctest::Approx(0.0f).epsilon(0.001));
	tally = StepAndCount(*world, 6.0f);
	CHECK(tally[EventType::Fired] == 0);
}

TEST_CASE("sim: a laser burns its target while the capacitor pays")
{
	auto world = MakeWorld();
	const ShipHandle shooter = SpawnAt(*world, Fitted({"laser_s"}), {}, PLAYERS);
	const ShipHandle target = SpawnAt(*world, Ship{}, {0.0f, 50.0f}, BOTS);
	const Module& laser = GetShip(*world, shooter)->modules[0];
	const f32 dps = laser.type.damage.amount[u32(DamageType::Em)];
	StepFor(*world, 2.0f);
	CHECK(laser.working);
	// EM: all of it to the shield, none to the hull, no events.
	const Ship& burnt = *GetShip(*world, target);
	const f32 taken = burnt.hull.shield + 2.0f * burnt.hull.shieldRegen - burnt.shield;
	CHECK(taken == doctest::Approx(2.0f * dps).epsilon(0.05));
	CHECK(burnt.health == burnt.hull.health);
	const Tally quiet = StepAndCount(*world, 0.5f);
	CHECK(quiet[EventType::ShieldHit] + quiet[EventType::HullHit] == 0);

	// Out of energy, it goes dark.
	GetShip(*world, shooter)->capacitor = 0.0f;
	GetShip(*world, shooter)->hull.reactor = 0.0f;
	Step(*world);
	CHECK(!laser.working);

	// A rock on the line takes the beam instead.
	GetShip(*world, shooter)->hull.reactor = 100.0f;
	GetShip(*world, shooter)->capacitor = 2.0f;
	AddRock(*world, {0.0f, 25.0f}, 3.0f, 1000.0f);
	const f32 shield = burnt.shield;
	StepFor(*world, 1.0f);
	CHECK(world->rocks[0].health < 1000.0f);
	CHECK(burnt.shield >= shield);
}

TEST_CASE("sim: the capacitor charges by the reactor's spare power")
{
	auto world = MakeWorld();
	const ShipHandle handle = SpawnAt(*world, Fitted({"laser_s", "shield_booster_s"}), {}, PLAYERS);
	Ship& ship = *GetShip(*world, handle);
	const FitStats stats = StatsOf(ship);
	CHECK(stats.load == 20.0f);
	CHECK(stats.recharge == doctest::Approx(0.08f));
	ship.capacitor = 0.0f;
	StepFor(*world, 1.0f);
	CHECK(ship.capacitor == doctest::Approx(0.08f).epsilon(0.02));
	StepFor(*world, 60.0f);
	CHECK(ship.capacitor == ship.hull.capacitor);
}

TEST_CASE("sim: an overloaded reactor drains the capacitor, then takes modules offline")
{
	auto world = MakeWorld();
	const ShipHandle handle = SpawnAt(*world, Fitted({"laser_s", "shield_booster_s"}), {}, PLAYERS);
	Ship& ship = *GetShip(*world, handle);
	ship.hull.reactor = 10.0f; // the modules draw 20 MW
	ship.capacitor = 0.05f;
	StepFor(*world, 6.0f);
	CHECK(ship.capacitor < 0.05f);
	// The last slot (the booster, 5 MW) goes first; still too much, so the
	// laser follows.
	CHECK(ship.modules[1].offline);
	CHECK(ship.modules[0].offline);
	CHECK(StatsOf(ship).Overloaded());
	// Back once the capacitor is half full again.
	ship.hull.reactor = 100.0f;
	StepFor(*world, 20.0f);
	CHECK(!ship.modules[0].offline);
	CHECK(!ship.modules[1].offline);
}

TEST_CASE("sim: a shield booster spends energy for shield, and keeps a reserve")
{
	auto world = MakeWorld();
	const ShipHandle handle = SpawnAt(*world, Fitted({"", "shield_booster_s"}), {}, PLAYERS);
	Ship& ship = *GetShip(*world, handle);
	const Module& booster = ship.modules[1];
	REQUIRE(booster.type.kind == ModuleKind::ShieldBooster);
	ship.shield = 100.0f;
	StepFor(*world, 1.0f);
	CHECK(booster.working);
	CHECK(ship.shield ==
	      doctest::Approx(100.0f + booster.type.boost + ship.hull.shieldRegen).epsilon(0.02));
	// Below its reserve it rests; at a full shield it rests too.
	ship.capacitor = 0.19f * ship.hull.capacitor;
	ship.hull.reactor = 5.0f;
	const f32 shield = ship.shield;
	Step(*world);
	CHECK(!booster.working);
	CHECK(ship.shield == doctest::Approx(shield + ship.hull.shieldRegen * TICK_SECONDS));
}

TEST_CASE("sim: hull hits wear out external modules, and a broken gun stops")
{
	auto world = MakeWorld();
	const ShipHandle victim = SpawnAt(*world, Fitted({"laser_s"}), {0.0f, 30.0f}, BOTS);
	const ShipHandle shooter =
		SpawnAt(*world, Fitted({"plasma_s"}, {{"plasma_charge_s", 300}}), {}, PLAYERS);
	Ship& ship = *GetShip(*world, victim);
	ship.shield = 0.0f;
	ship.hull.shield = 0.0f;
	ship.hull.shieldRegen = 0.0f;
	ship.hull.health = 1e6f;
	ship.health = 1e6f;
	GetShip(*world, shooter)->hull.health = 1e6f;
	GetShip(*world, shooter)->health = 1e6f;
	const Tally tally = StepAndCount(*world, 10.0f);
	CHECK(tally[EventType::HullHit] > 0);
	CHECK(tally[EventType::ModuleBroken] == 1);
	CHECK(ship.modules[0].health == 0.0f);
	CHECK(!ship.modules[0].working);
	CHECK(WeaponRange(ship) == 0.0f);
}

TEST_CASE("sim: turrets leave their own team alone")
{
	auto world = MakeWorld();
	SpawnAt(*world, Fitted({"plasma_s"}, {{"plasma_charge_s", 100}}), {}, BOTS);
	const ShipHandle friendly = SpawnAt(*world, Ship{}, {0.0f, 20.0f}, BOTS);
	const Tally tally = StepAndCount(*world, 2.0f);
	CHECK(tally[EventType::Fired] == 0);
	CHECK(GetShip(*world, friendly)->shield == HullClass{}.shield);
}

TEST_CASE("sim: the chosen target comes first while in range")
{
	auto world = MakeWorld();
	const ShipHandle shooter =
		SpawnAt(*world, Fitted({"plasma_s"}, {{"plasma_charge_s", 100}}), {}, PLAYERS);
	const ShipHandle near = SpawnAt(*world, Ship{}, {0.0f, 20.0f}, BOTS);
	const ShipHandle far = SpawnAt(*world, Ship{}, {60.0f, 0.0f}, BOTS);
	StepFor(*world, 0.5f);
	CHECK(GetShip(*world, shooter)->modules[0].target == near);
	SetTarget(*world, shooter, far);
	StepFor(*world, 0.5f);
	CHECK(GetShip(*world, shooter)->modules[0].target == far);
	// Out of range, the nearest again.
	GetShip(*world, far)->position = {500.0f, 0.0f};
	StepFor(*world, 0.1f);
	CHECK(GetShip(*world, shooter)->modules[0].target == near);
}

TEST_CASE("sim: ships destroyed, then whole again")
{
	auto world = MakeWorld();
	const ShipHandle shooter = SpawnAt(*world, Fitted({"laser_s"}), {}, PLAYERS);
	Ship weak;
	weak.hull.health = 20.0f;
	weak.hull.shield = 10.0f;
	const ShipHandle target = SpawnAt(*world, weak, {0.0f, 30.0f}, BOTS);
	const Tally tally = StepAndCount(*world, 4.0f);
	CHECK(tally[EventType::ShipDestroyed] == 1);
	const Ship* wreck = GetShip(*world, target);
	REQUIRE(wreck); // the server removes or revives wrecks
	CHECK(!IsAlive(*wreck));
	CHECK(!GetShip(*world, shooter)->modules[0].working);
	// A wreck neither moves nor is hit.
	SetControls(*world, target, {0.0f, 1.0f});
	StepFor(*world, 1.0f);
	CHECK(GetShip(*world, target)->position.y == 30.0f);

	ReviveShip(*world, target, {0.0f, 30.0f}, 0.0f);
	CHECK(IsAlive(*GetShip(*world, target)));
	CHECK(GetShip(*world, target)->shield == weak.hull.shield);
}

TEST_CASE("sim: players' ships take crates within reach, while their holds have room")
{
	auto world = MakeWorld();
	Ship ship;
	ship.hull.cargo = 1.0f;
	const ShipHandle player = SpawnAt(*world, ship, {}, PLAYERS);
	const ShipHandle bot = SpawnAt(*world, ship, {100.0f, 0.0f}, BOTS);
	const u32 near = DropCrate(*world, {4.0f, 0.0f}, 0.6f, 0.3f, 10.0f);
	const u32 big = DropCrate(*world, {-4.0f, 0.0f}, 0.6f, 0.3f, 10.0f);
	DropCrate(*world, {100.0f, 3.0f}, 0.1f, 0.1f, 1.0f); // the bot's: never taken
	CHECK(world->eventCount == 3);
	CHECK(near != NO_INDEX);
	const Tally tally = StepAndCount(*world, 0.5f);
	CHECK(tally[EventType::CratePicked] == 1);
	const Ship& state = *GetShip(*world, player);
	CHECK(state.cargoUsed == doctest::Approx(0.6f));
	CHECK(state.cargoMass == doctest::Approx(0.3f));
	CHECK(world->crates[near].life == 0.0f);
	CHECK(world->crates[big].life > 0.0f); // no room left for it
	CHECK(GetShip(*world, bot)->cargoUsed == 0.0f);
	const Tally later = StepAndCount(*world, 1.0f);
	CHECK(later[EventType::CrateLost] == 1);
}

TEST_CASE("sim: ships bounce off each other")
{
	auto world = MakeWorld();
	Ship ship;
	ship.position = {0.0f, -6.0f};
	ship.velocity = {0.0f, 20.0f};
	const ShipHandle first = SpawnShip(*world, ship);
	ship.position = {0.0f, 6.0f};
	ship.velocity = {0.0f, -20.0f};
	ship.angle = ph::PI;
	const ShipHandle second = SpawnShip(*world, ship);
	bool bumped = false;
	for (u32 i = 0; i < TICK_RATE; ++i)
	{
		Step(*world);
		const Vec2 apart = GetShip(*world, first)->position - GetShip(*world, second)->position;
		CHECK(Length(apart) >= 2.0f * HullClass{}.radius - 0.001f);
		for (u32 e = 0; e < world->eventCount; ++e)
			bumped |= world->events[e].type == EventType::ShipBumped &&
			          world->events[e].ship == first && world->events[e].other == second &&
			          world->events[e].index == NO_INDEX;
	}
	CHECK(bumped);
	CHECK(GetShip(*world, first)->velocity.y < 0.0f); // turned back
	CHECK(GetShip(*world, second)->velocity.y > 0.0f);
}

TEST_CASE("sim: a ship flown alone flies as it does in the world")
{
	// What a client's prediction does (FlyShip, BounceOffRocks) must be
	// exactly what Step does to the same ship.
	auto world = MakeWorld();
	world->rocks[0] = {{0.0f, 20.0f}, 3.0f, 100.0f};
	world->rockCount = 1;
	const ShipHandle handle = SpawnShip(*world, Fitted({"laser_s"}, {{"metal", 100}}));
	Ship alone = *GetShip(*world, handle);
	u32 bumps = 0;
	for (u32 tick = 0; tick < 6 * TICK_RATE; ++tick)
	{
		ShipControls controls;
		controls.thrust = tick % 90 < 60 ? 1.0f : -1.0f;
		controls.turn = tick < 45 ? 0.0f : std::sin(f32(tick) * 0.05f);
		SetControls(*world, handle, controls);
		Step(*world);
		alone.controls = controls;
		FlyShip(alone);
		bumps += BounceOffRocks(alone, world->rocks, world->rockCount, nullptr, 0);
		const Ship& stepped = *GetShip(*world, handle);
		CHECK(alone.position.x == stepped.position.x);
		CHECK(alone.position.y == stepped.position.y);
		CHECK(alone.velocity.y == stepped.velocity.y);
		CHECK(alone.angle == stepped.angle);
	}
	CHECK(bumps > 0);
}

TEST_CASE("sim: leading a moving target")
{
	// A target crossing at 10 m/s, 50 m ahead; shots at 100 m/s meet it.
	const Vec2 from = {};
	const Vec2 at = {0.0f, 50.0f};
	const Vec2 velocity = {10.0f, 0.0f};
	const Vec2 aim = Lead(from, {}, 100.0f, at, velocity);
	const f32 time = Length(aim - from) / 100.0f;
	CHECK(aim.x == doctest::Approx(velocity.x * time).epsilon(0.01));
	CHECK(aim.y == doctest::Approx(50.0f));
	// Too fast to catch: aim at it.
	const Vec2 hopeless = Lead(from, {}, 5.0f, at, {0.0f, 50.0f});
	CHECK(hopeless.y == at.y);
}
