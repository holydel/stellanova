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
