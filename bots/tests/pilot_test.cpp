#include <sn/bots/pilot.h>
#include <sn/sim/catalog.h>
#include <sn/sim/fitting.h>

#include <doctest/doctest.h>

#include <cmath>
#include <memory>

using namespace sn;
using namespace sn::sim;

namespace
{
// Worlds hold every pool inline: too big for a test's stack.
std::unique_ptr<World> MakeWorld() { return std::make_unique<World>(); }

Ship WithLaser()
{
	ShipPlan plan;
	plan.hull = "lancer";
	plan.modules = {"laser_s"};
	Ship ship;
	CHECK(MakeShip(GetCatalog(), FitFromPlan(GetCatalog(), plan), ship));
	return ship;
}

ShipHandle Spawn(World& world, Vec2 position, u8 team, Ship ship = {})
{
	ship.position = position;
	ship.team = team;
	return SpawnShip(world, ship);
}

// Follows an order for `seconds`; returns the bumps into rocks.
u32 Follow(World& world, ShipHandle handle, const bots::Order& order, f32 seconds,
           ShipHandle target = {}, f32 distance = 0.0f)
{
	u32 bumps = 0;
	for (u32 i = 0; i < u32(seconds * f32(TICK_RATE)); ++i)
	{
		const Ship* other = GetShip(world, target);
		const bots::Mark mark = other ? bots::Mark{other->position, other->velocity} : bots::Mark{};
		SetControls(world, handle,
		            bots::Steer(*GetShip(world, handle), order, other ? &mark : nullptr, distance,
		                        world.rocks, world.rockCount));
		Step(world);
		for (u32 e = 0; e < world.eventCount; ++e)
			bumps += world.events[e].type == EventType::ShipBumped &&
			         world.events[e].ship == handle && world.events[e].index != NO_INDEX;
	}
	return bumps;
}
} // namespace

TEST_CASE("orders: a ship flies to a point and stops there")
{
	auto world = MakeWorld();
	const ShipHandle ship = Spawn(*world, {}, PLAYERS);
	bots::Order order;
	order.kind = bots::Order::Kind::Move;
	order.point = {60.0f, -40.0f}; // behind it and to the side
	Follow(*world, ship, order, 12.0f);
	const Ship& state = *GetShip(*world, ship);
	CHECK(Length(state.position - order.point) < 3.0f);
	CHECK(Length(state.velocity) < 2.0f);

	// Stop: it slows down where it is.
	GetShip(*world, ship)->velocity = {30.0f, 0.0f};
	Follow(*world, ship, {}, 4.0f);
	CHECK(Length(GetShip(*world, ship)->velocity) < 1.0f);
}

TEST_CASE("orders: a ship flies around a rock in its way")
{
	auto world = MakeWorld();
	world->rocks[world->rockCount++] = {{0.0f, 35.0f}, 6.0f, RockHealth(6.0f)};
	const ShipHandle ship = Spawn(*world, {}, PLAYERS);
	bots::Order order;
	order.kind = bots::Order::Kind::Move;
	order.point = {0.0f, 80.0f};
	CHECK(Follow(*world, ship, order, 8.0f) == 0);
	CHECK(Length(GetShip(*world, ship)->position - order.point) < 4.0f);
}

TEST_CASE("orders: an attack closes in and circles the target at its distance")
{
	auto world = MakeWorld();
	const ShipHandle ship = Spawn(*world, {}, PLAYERS, WithLaser());
	const ShipHandle target = Spawn(*world, {0.0f, 160.0f}, BOTS);
	GetShip(*world, target)->hull.health = 1e6f;
	GetShip(*world, target)->health = 1e6f;
	const f32 distance = bots::AttackDistance(*GetShip(*world, ship), 0.8f);
	CHECK(distance == doctest::Approx(80.0f));
	bots::Order order;
	order.kind = bots::Order::Kind::Attack;
	Follow(*world, ship, order, 10.0f, target, distance);
	// Around it, at the distance, still moving; its laser on it.
	f32 nearest = 1e9f;
	f32 farthest = 0.0f;
	for (u32 i = 0; i < 4; ++i)
	{
		Follow(*world, ship, order, 1.0f, target, distance);
		const f32 apart =
			Length(GetShip(*world, ship)->position - GetShip(*world, target)->position);
		nearest = std::min(nearest, apart);
		farthest = std::max(farthest, apart);
	}
	CHECK(nearest > 0.7f * distance);
	CHECK(farthest < 1.2f * distance);
	CHECK(Length(GetShip(*world, ship)->velocity) > 10.0f);
	CHECK(GetShip(*world, target)->shield < GetShip(*world, target)->hull.shield);
}

TEST_CASE("bots: a bot attacks the nearest enemy, never its own team")
{
	auto world = MakeWorld();
	const ShipHandle friendly = Spawn(*world, {0.0f, 20.0f}, BOTS);
	const ShipHandle bot = Spawn(*world, {}, BOTS, WithLaser());
	const ShipHandle player = Spawn(*world, {150.0f, 0.0f}, PLAYERS);
	const ShipHandle far = Spawn(*world, {-250.0f, 0.0f}, PLAYERS);
	bots::Pilot pilot;
	const bots::PilotSkill skill;
	for (u32 i = 0; i < 12 * TICK_RATE; ++i)
	{
		SetControls(*world, bot, bots::Fly(*world, bot, pilot, skill));
		SetTarget(*world, bot, pilot.target);
		Step(*world);
	}
	CHECK(pilot.target == player);
	CHECK(GetShip(*world, player)->shield < GetShip(*world, player)->hull.shield);
	CHECK(GetShip(*world, far)->shield == GetShip(*world, far)->hull.shield);
	CHECK(GetShip(*world, friendly)->shield == GetShip(*world, friendly)->hull.shield);
}

TEST_CASE("bots: alone, a bot roams")
{
	auto world = MakeWorld();
	const ShipHandle bot = Spawn(*world, {}, BOTS);
	bots::Pilot pilot;
	for (u32 i = 0; i < 5 * TICK_RATE; ++i)
	{
		SetControls(*world, bot, bots::Fly(*world, bot, pilot, {}));
		Step(*world);
	}
	CHECK(!pilot.target);
	CHECK(Length(GetShip(*world, bot)->position) > 10.0f);
}
