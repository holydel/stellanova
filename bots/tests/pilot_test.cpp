#include <sn/bots/pilot.h>

#include <doctest/doctest.h>

#include <cmath>
#include <memory>

using namespace sn;
using namespace sn::sim;

namespace
{
// Worlds hold every pool inline: too big for a test's stack.
std::unique_ptr<World> MakeWorld() { return std::make_unique<World>(); }

ShipHandle Spawn(World& world, Vec2 position, u8 team, f32 angle = 0.0f)
{
	Ship ship;
	ship.position = position;
	ship.angle = angle;
	ship.team = team;
	return SpawnShip(world, ship);
}

struct Tally
{
	u32 fired = 0; // by the bot
	u32 hits = 0;  // on the target, shield or hull
	u32 bumps = 0; // the bot's, into rocks
	bool destroyed = false;
};

// The bot flies for `seconds`, or until the target is destroyed.
Tally FlyFor(World& world, ShipHandle bot, bots::Pilot& pilot, const bots::PilotSkill& skill,
             ShipHandle target, f32 seconds)
{
	Tally tally;
	for (u32 i = 0; i < u32(seconds * f32(TICK_RATE)) && !tally.destroyed; ++i)
	{
		SetControls(world, bot, bots::Fly(world, bot, pilot, skill));
		Step(world);
		for (u32 e = 0; e < world.eventCount; ++e)
		{
			const Event& event = world.events[e];
			tally.fired += event.type == EventType::Fired && event.ship == bot;
			tally.hits +=
				(event.type == EventType::ShieldHit || event.type == EventType::HullHit) &&
				event.ship == target;
			tally.bumps +=
				event.type == EventType::ShipBumped && event.ship == bot && event.rock != NO_ROCK;
			tally.destroyed |= event.type == EventType::ShipDestroyed && event.ship == target;
		}
	}
	return tally;
}
} // namespace

TEST_CASE("bots: the lead meets a moving target")
{
	// A target 40 m ahead, crossing at 20 m/s; shots at 90 m/s.
	const Vec2 from = {0.0f, 0.0f};
	const Vec2 at = {0.0f, 40.0f};
	const Vec2 crossing = {20.0f, 0.0f};
	const Vec2 lead = bots::Lead(from, {}, 90.0f, at, crossing);
	CHECK(lead.x > 0.0f);
	// A shot toward the lead gets there when the target does.
	const f32 shotTime = Length(lead - from) / 90.0f;
	const f32 targetTime = Length(lead - at) / 20.0f;
	CHECK(shotTime == doctest::Approx(targetTime).epsilon(0.001));
	// Still targets: aim at them. Out of reach: aim at them too.
	CHECK(bots::Lead(from, {}, 90.0f, at, {}).y == doctest::Approx(40.0f));
	CHECK(bots::Lead(from, {}, 10.0f, at, {0.0f, 50.0f}).y == 40.0f);
}

TEST_CASE("bots: a bot turns to its target and fires once it faces it")
{
	auto world = MakeWorld();
	const ShipHandle target = Spawn(*world, {30.0f, 0.0f}, PLAYERS);
	const ShipHandle bot = Spawn(*world, {}, BOTS); // facing +y: the target is to its right
	bots::Pilot pilot;
	bots::PilotSkill skill;
	skill.aimError = 0.0f;
	// The first ticks turn it without firing.
	SetControls(*world, bot, bots::Fly(*world, bot, pilot, skill));
	CHECK(GetShip(*world, bot)->controls.turn < 0.0f); // right is negative
	CHECK(!GetShip(*world, bot)->controls.fire);
	const Tally tally = FlyFor(*world, bot, pilot, skill, target, 1.5f);
	CHECK(pilot.target == target);
	CHECK(tally.fired >= 2);
	CHECK(tally.hits >= 1);
}

TEST_CASE("bots: a bot never fires at its own team")
{
	auto world = MakeWorld();
	const ShipHandle friendly = Spawn(*world, {0.0f, 30.0f}, BOTS);
	const ShipHandle bot = Spawn(*world, {}, BOTS);
	bots::Pilot pilot;
	const Tally tally = FlyFor(*world, bot, pilot, {}, friendly, 3.0f);
	CHECK(!pilot.target);
	CHECK(tally.fired == 0);
}

TEST_CASE("bots: a bot flies around a rock in its way")
{
	// The target is straight ahead, behind a rock.
	auto world = MakeWorld();
	world->rocks[world->rockCount++] = {{0.0f, 35.0f}, 6.0f, RockHealth(6.0f)};
	const ShipHandle target = Spawn(*world, {0.0f, 90.0f}, PLAYERS);
	GetShip(*world, target)->hull.health = 1000.0f; // it outlasts the test
	GetShip(*world, target)->health = 1000.0f;
	const ShipHandle bot = Spawn(*world, {}, BOTS);
	bots::Pilot pilot;
	bots::PilotSkill skill;
	skill.range = 0.0f; // no firing: the rock stays
	const Tally tally = FlyFor(*world, bot, pilot, skill, target, 5.0f);
	CHECK(tally.bumps == 0);
	CHECK(world->rocks[0].health > 0.0f);
	// It got past the rock, near its target.
	CHECK(GetShip(*world, bot)->position.y > 45.0f);
}

TEST_CASE("bots: a bot destroys a ship that holds still, in a few passes")
{
	auto world = MakeWorld();
	const ShipHandle target = Spawn(*world, {20.0f, 60.0f}, PLAYERS);
	const ShipHandle bot = Spawn(*world, {}, BOTS);
	bots::Pilot pilot;
	bots::PilotSkill skill;
	skill.aimError = 0.02f;
	const Tally tally = FlyFor(*world, bot, pilot, skill, target, 30.0f);
	CHECK(tally.destroyed);
	CHECK(!IsAlive(*GetShip(*world, target)));
}
