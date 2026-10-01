#include <sn/server/server.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <doctest/doctest.h>

#include <memory>

using namespace sn;
using namespace ph;

TEST_CASE("protocol: messages read back, and refuse other bytes")
{
	sim::Input input;
	input.tick = 42;
	input.controls = {-0.5f, 1.0f};
	const std::vector<u8> bytes = sim::Write(input);
	CHECK(sim::TypeOf(bytes.data(), bytes.size()) == sim::MessageType::Input);
	sim::Input back;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), back));
	CHECK(back.tick == 42);
	CHECK(back.controls.turn == -0.5f);
	sim::Welcome welcome;
	CHECK(!sim::Read(bytes.data(), bytes.size(), welcome));  // another type
	CHECK(!sim::Read(bytes.data(), bytes.size() - 1, back)); // cut short

	auto world = std::make_unique<sim::World>();
	sim::Ship ship;
	ship.position = {3.0f, 4.0f};
	const sim::ShipHandle handle = sim::SpawnShip(*world, ship);
	auto snapshot = std::make_unique<sim::Snapshot>();
	sim::TakeSnapshot(*world, *snapshot);
	const std::vector<u8> snapshotBytes = sim::Write(*snapshot);
	CHECK(snapshotBytes.size() <= net::MAX_UNRELIABLE_BYTES);
	auto read = std::make_unique<sim::Snapshot>();
	REQUIRE(sim::Read(snapshotBytes.data(), snapshotBytes.size(), *read));
	REQUIRE(read->count == 1);
	const sim::ShipState* state = read->Find(sim::ShipId(handle));
	REQUIRE(state);
	CHECK(state->position.y == 4.0f);
	CHECK(sim::ShipFromId(state->id) == handle);
}

TEST_CASE("server: a local client joins, flies its ship, and leaves")
{
	server::Server host;
	REQUIRE(host.Start("loopback:server-test"));
	const net::Client client = net::Connect("loopback:server-test");
	net::Event event;
	REQUIRE(net::Poll(client, event));
	REQUIRE(event.type == net::EventType::Connected);
	const std::vector<u8> hello = sim::Write(sim::Hello{});
	REQUIRE(net::Send(client, hello.data(), u32(hello.size()), net::Delivery::Reliable));

	host.Update(0.0f);
	REQUIRE(net::Poll(client, event));
	sim::Welcome welcome;
	REQUIRE(sim::Read(event.data, event.size, welcome));
	CHECK(welcome.ship != 0);

	// Full thrust for a second: 30 ticks, a snapshot after each.
	sim::Input input;
	input.controls.thrust = 1.0f;
	const std::vector<u8> bytes = sim::Write(input);
	REQUIRE(net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Unreliable));
	host.Update(1.0f / 60.0f); // takes the input
	for (int i = 0; i < 60; ++i)
		host.Update(1.0f / 60.0f);
	auto latest = std::make_unique<sim::Snapshot>();
	int snapshots = 0;
	while (net::Poll(client, event))
	{
		if (sim::Read(event.data, event.size, *latest))
			++snapshots;
	}
	CHECK(snapshots >= 30);
	const sim::ShipState* ship = latest->Find(welcome.ship);
	REQUIRE(ship);
	CHECK(ship->position.y > 5.0f); // up the screen, at angle 0
	CHECK(ship->controls.thrust == 1.0f);

	// Leaving removes the ship.
	net::Close(client);
	host.Update(0.0f);
	CHECK(!sim::GetShip(*host.GetWorld(), sim::ShipFromId(welcome.ship)));
	host.Stop();
}
