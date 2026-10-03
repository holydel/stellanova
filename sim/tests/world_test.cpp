#include <sn/sim/world.h>

#include <doctest/doctest.h>

#include <cmath>
#include <memory>

using namespace sn::sim;

namespace
{
// Worlds hold every pool inline: too big for a test's stack.
std::unique_ptr<World> MakeWorld() { return std::make_unique<World>(); }

void StepFor(World& world, f32 seconds)
{
	for (u32 i = 0; i < u32(seconds * f32(TICK_RATE) + 0.5f); ++i)
		Step(world);
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

TEST_CASE("sim: turning, drag and reverse")
{
	auto world = MakeWorld();
	const ShipHandle ship = SpawnShip(*world, {});
	// A quarter turn left: it faces -x.
	SetControls(*world, ship, {1.0f, 0.0f});
	StepFor(*world, 0.5f * ph::PI / HullClass{}.turnRate);
	CHECK(GetShip(*world, ship)->angle == doctest::Approx(0.5f * ph::PI).epsilon(0.05));
	const Vec2 forward = Forward(GetShip(*world, ship)->angle);
	CHECK(forward.x == doctest::Approx(-1.0f).epsilon(0.01));

	// Without thrust, speed fades away.
	GetShip(*world, ship)->velocity = {10.0f, 0.0f};
	SetControls(*world, ship, {});
	StepFor(*world, 10.0f);
	CHECK(std::abs(GetShip(*world, ship)->velocity.x) < 0.1f);

	// Reverse is weaker than forward.
	GetShip(*world, ship)->angle = 0.0f;
	GetShip(*world, ship)->velocity = {};
	SetControls(*world, ship, {0.0f, -1.0f});
	Step(*world);
	const f32 backward = -GetShip(*world, ship)->velocity.y;
	GetShip(*world, ship)->velocity = {};
	SetControls(*world, ship, {0.0f, 1.0f});
	Step(*world);
	CHECK(backward > 0.0f);
	CHECK(backward < GetShip(*world, ship)->velocity.y);
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

namespace
{
// The events of the next `seconds`, counted by type.
struct Tally
{
	u32 fired = 0;
	u32 hits = 0;
	u32 broken = 0;
	u32 bumps = 0;
	u32 shieldHits = 0;
	u32 hullHits = 0;
	u32 destroyed = 0;
};

Tally StepAndCount(World& world, f32 seconds)
{
	Tally tally;
	for (u32 i = 0; i < u32(seconds * f32(TICK_RATE) + 0.5f); ++i)
	{
		Step(world);
		for (u32 e = 0; e < world.eventCount; ++e)
		{
			switch (world.events[e].type)
			{
				case EventType::Fired: ++tally.fired; break;
				case EventType::RockHit: ++tally.hits; break;
				case EventType::RockBroken: ++tally.broken; break;
				case EventType::ShipBumped: ++tally.bumps; break;
				case EventType::ShieldHit: ++tally.shieldHits; break;
				case EventType::HullHit: ++tally.hullHits; break;
				case EventType::ShipDestroyed: ++tally.destroyed; break;
			}
		}
	}
	return tally;
}

void AddRock(World& world, Vec2 position, f32 radius)
{
	world.rocks[world.rockCount++] = {position, radius, RockHealth(radius)};
}
} // namespace

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
	CHECK(tally.bumps >= 1);
	CHECK(state->velocity.y < 0.0f); // turned back
	// Never inside: on the rock's edge at most.
	CHECK(state->position.y <= 12.0f - 3.0f - state->hull.radius + 0.001f);
}

TEST_CASE("sim: the gun fires at its interval, and shots break rocks")
{
	auto world = MakeWorld();
	AddRock(*world, {0.0f, 25.0f}, 3.0f); // three hits
	const ShipHandle handle = SpawnShip(*world, {});
	SetControls(*world, handle, {0.0f, 0.0f, true});
	const Tally tally = StepAndCount(*world, 1.2f);
	const WeaponClass weapon;
	// The first shot at once, then one each interval, rounded to ticks.
	CHECK(tally.fired >= u32(1.2f / weapon.interval) - 2);
	CHECK(tally.fired <= u32(1.2f / weapon.interval) + 1);
	CHECK(tally.hits == 3);
	CHECK(tally.broken == 1);
	CHECK(world->rocks[0].health == 0.0f);

	// A rock gone stops nothing: the next shots fly on, then fade.
	SetControls(*world, handle, {});
	StepAndCount(*world, 2.0f);
	for (const Shot& shot : world->shots)
		CHECK(shot.life <= 0.0f);
}

TEST_CASE("sim: a fast shot does not skip a small rock")
{
	// The shot's ends land 1.5 m before and after the rock's middle, which
	// only a sweep along its way catches.
	auto world = MakeWorld();
	AddRock(*world, {0.0f, 12.3f}, 1.2f);
	const ShipHandle handle = SpawnShip(*world, {});
	SetControls(*world, handle, {0.0f, 0.0f, true});
	Step(*world); // fired: from 1.8 m ahead to 4.8 m
	SetControls(*world, handle, {});
	const Tally tally = StepAndCount(*world, 0.2f);
	CHECK(tally.hits == 1);
	CHECK(tally.broken == 1);
}

namespace
{
ShipHandle SpawnAt(World& world, Vec2 position, u8 team)
{
	Ship ship;
	ship.position = position;
	ship.team = team;
	return SpawnShip(world, ship);
}
} // namespace

TEST_CASE("sim: shots hit ships of another team, the shield first, then the hull")
{
	auto world = MakeWorld();
	const ShipHandle shooter = SpawnAt(*world, {}, PLAYERS);
	const ShipHandle target = SpawnAt(*world, {0.0f, 20.0f}, BOTS);
	const HullClass hull;
	CHECK(GetShip(*world, target)->health == hull.health);
	CHECK(GetShip(*world, target)->shield == hull.shield);
	SetControls(*world, shooter, {0.0f, 0.0f, true});
	// One hit a shot: the shield's 8, then the hull's 12.
	const Tally tally = StepAndCount(*world, 4.0f);
	CHECK(tally.shieldHits == u32(hull.shield));
	CHECK(tally.hullHits == u32(hull.health));
	CHECK(tally.destroyed == 1);
	const Ship* wreck = GetShip(*world, target);
	REQUIRE(wreck); // the server removes or revives wrecks
	CHECK(!IsAlive(*wreck));
	CHECK(wreck->health == 0.0f);
	// Shots fly through a wreck; it neither moves nor fires.
	CHECK(tally.fired > tally.shieldHits + tally.hullHits);
	SetControls(*world, target, {0.0f, 1.0f, true});
	const Tally after = StepAndCount(*world, 1.0f);
	CHECK(after.shieldHits + after.hullHits == 0);
	CHECK(GetShip(*world, target)->position.y == 20.0f);

	// Who hit whom: the events name both.
	SetControls(*world, shooter, {});
	ReviveShip(*world, target, {0.0f, 20.0f}, 0.0f);
	CHECK(IsAlive(*GetShip(*world, target)));
	CHECK(GetShip(*world, target)->shield == hull.shield);
	SetControls(*world, shooter, {0.0f, 0.0f, true});
	bool named = false;
	for (u32 i = 0; i < TICK_RATE && !named; ++i)
	{
		Step(*world);
		for (u32 e = 0; e < world->eventCount; ++e)
			named |= world->events[e].type == EventType::ShieldHit &&
			         world->events[e].ship == target && world->events[e].other == shooter;
	}
	CHECK(named);
}

TEST_CASE("sim: ships of a team never hit each other")
{
	auto world = MakeWorld();
	const ShipHandle shooter = SpawnAt(*world, {}, BOTS);
	const ShipHandle friendly = SpawnAt(*world, {0.0f, 20.0f}, BOTS);
	SetControls(*world, shooter, {0.0f, 0.0f, true});
	const Tally tally = StepAndCount(*world, 1.0f);
	CHECK(tally.fired > 0);
	CHECK(tally.shieldHits + tally.hullHits == 0);
	CHECK(GetShip(*world, friendly)->shield == HullClass{}.shield);
}

TEST_CASE("sim: the shield comes back after a rest")
{
	auto world = MakeWorld();
	const ShipHandle ship = SpawnAt(*world, {}, PLAYERS);
	const HullClass hull;
	GetShip(*world, ship)->shield = 1.0f;
	GetShip(*world, ship)->rested = 0.0f; // just hit
	StepFor(*world, hull.shieldDelay - 0.5f);
	CHECK(GetShip(*world, ship)->shield == 1.0f);
	StepFor(*world, 1.5f);
	CHECK(GetShip(*world, ship)->shield > 1.0f);
	StepFor(*world, 10.0f);
	CHECK(GetShip(*world, ship)->shield == hull.shield);
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
			          world->events[e].rock == NO_ROCK;
	}
	CHECK(bumped);
	CHECK(GetShip(*world, first)->velocity.y < 0.0f); // turned back
	CHECK(GetShip(*world, second)->velocity.y > 0.0f);
}

TEST_CASE("sim: a ship flown alone flies as it does in the world")
{
	// What a client's prediction does (FlyShip, FireGun, BounceOffRocks)
	// must be exactly what Step does to the same ship.
	auto world = MakeWorld();
	world->rocks[0] = {{0.0f, 20.0f}, 3.0f, 100.0f};
	world->rocks[1] = {{0.0f, 8.0f}, 1.0f, 1.0f}; // the first shot breaks it
	world->rockCount = 2;
	const ShipHandle handle = SpawnShip(*world, {});
	Ship alone = *GetShip(*world, handle);
	u32 bumps = 0;
	u32 shots = 0;
	for (u32 tick = 0; tick < 6 * TICK_RATE; ++tick)
	{
		ShipControls controls;
		controls.thrust = tick % 90 < 60 ? 1.0f : -1.0f;
		controls.turn = tick < 45 ? 0.0f : std::sin(f32(tick) * 0.05f);
		controls.fire = tick % 20 < 8;
		SetControls(*world, handle, controls);
		Step(*world);
		alone.controls = controls;
		FlyShip(alone);
		Shot shot;
		shots += FireGun(alone, shot);
		bumps += BounceOffRocks(alone, world->rocks, world->rockCount, nullptr, 0);
		const Ship& stepped = *GetShip(*world, handle);
		CHECK(alone.position.x == stepped.position.x);
		CHECK(alone.position.y == stepped.position.y);
		CHECK(alone.velocity.y == stepped.velocity.y);
		CHECK(alone.angle == stepped.angle);
		CHECK(alone.cooldown == stepped.cooldown);
	}
	CHECK(bumps > 0);
	CHECK(shots > 10);
	CHECK(world->rocks[1].health == 0.0f); // shot away on the way: the alone one saw it go too
}
