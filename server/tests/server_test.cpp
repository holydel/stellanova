#include <sn/server/server.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <doctest/doctest.h>

#include <memory>
#include <string>

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
	ship.team = sim::BOTS;
	const sim::ShipHandle handle = sim::SpawnShip(*world, ship);
	sim::GetShip(*world, handle)->shield = 0.5f * ship.hull.shield;
	world->shots[3] = {{1.0f, 1.0f}, {0.0f, 90.0f}, 1.0f, 0.3f, 1.0f, handle, sim::BOTS};
	auto snapshot = std::make_unique<sim::Snapshot>();
	sim::TakeSnapshot(*world, *snapshot);
	snapshot->wave = 4;
	const std::vector<u8> snapshotBytes = sim::Write(*snapshot);
	CHECK(snapshotBytes.size() <= net::MAX_UNRELIABLE_BYTES);
	auto read = std::make_unique<sim::Snapshot>();
	REQUIRE(sim::Read(snapshotBytes.data(), snapshotBytes.size(), *read));
	REQUIRE(read->count == 1);
	const sim::ShipState* state = read->Find(sim::ShipId(handle));
	REQUIRE(state);
	CHECK(state->position.y == 4.0f);
	CHECK(sim::ShipFromId(state->id) == handle);
	CHECK(state->team == sim::BOTS);
	CHECK(state->health == 255);
	CHECK(state->shield == 128);
	CHECK(read->wave == 4);
	REQUIRE(read->shotCount == 1);
	CHECK(read->shots[0].team == sim::BOTS);
	// A sliver of health is not a wreck.
	CHECK(sim::Share(0.001f, 12.0f) == 1);
	CHECK(sim::Share(0.0f, 12.0f) == 0);
}

TEST_CASE("protocol: the field, shots and events read back")
{
	sim::Welcome welcome;
	welcome.ship = 7;
	welcome.respawn = 2.5f;
	welcome.rocks = {{{1.0f, 2.0f}, 3.0f, 3.0f}, {{-5.0f, 6.0f}, 1.5f, 0.0f}};
	std::vector<u8> bytes = sim::Write(welcome);
	sim::Welcome welcomeBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), welcomeBack));
	CHECK(welcomeBack.respawn == 2.5f);
	REQUIRE(welcomeBack.rocks.size() == 2);
	CHECK(welcomeBack.rocks[1].position.x == -5.0f);
	CHECK(welcomeBack.rocks[1].health == 0.0f);

	sim::Input input;
	input.controls = {0.0f, 1.0f, true};
	bytes = sim::Write(input);
	sim::Input inputBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), inputBack));
	CHECK(inputBack.controls.fire);
	bytes.back() = 2; // fire is 0 or 1
	CHECK(!sim::Read(bytes.data(), bytes.size(), inputBack));

	sim::Events events;
	events.tick = 9;
	events.events = {{sim::EventType::RockBroken, 7, 0, 1, {-5.0f, 6.0f}, 1.5f},
	                 {sim::EventType::HullHit, 7, 9, sim::NO_ROCK, {1.0f, 2.0f}, 1.0f}};
	bytes = sim::Write(events);
	sim::Events eventsBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), eventsBack));
	REQUIRE(eventsBack.events.size() == 2);
	CHECK(eventsBack.events[0].type == sim::EventType::RockBroken);
	CHECK(eventsBack.events[0].rock == 1);
	CHECK(eventsBack.events[1].type == sim::EventType::HullHit);
	CHECK(eventsBack.events[1].other == 9);
	CHECK(eventsBack.events[1].rock == sim::NO_ROCK);
	bytes[1 + 8 + 4] = 99; // no such event
	CHECK(!sim::Read(bytes.data(), bytes.size(), eventsBack));

	// Many ships and shots: the snapshot keeps within one unreliable message.
	auto world = std::make_unique<sim::World>();
	for (u32 i = 0; i < 40; ++i)
		sim::SpawnShip(*world, {});
	for (u32 i = 0; i < 200; ++i)
		world->shots[i].life = 1.0f;
	auto snapshot = std::make_unique<sim::Snapshot>();
	sim::TakeSnapshot(*world, *snapshot);
	CHECK(snapshot->count == sim::MAX_SNAPSHOT_SHIPS);
	CHECK(snapshot->shotCount > 0);
	bytes = sim::Write(*snapshot);
	CHECK(bytes.size() <= net::MAX_UNRELIABLE_BYTES);
	auto back = std::make_unique<sim::Snapshot>();
	REQUIRE(sim::Read(bytes.data(), bytes.size(), *back));
	CHECK(back->shotCount == snapshot->shotCount);
}

TEST_CASE("server: a local client joins, flies its ship, and leaves")
{
	server::Server host;
	server::MatchDesc match;
	match.bots = false;
	REQUIRE(host.Start("loopback:server-test", match));
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
	CHECK(welcome.respawn == match.respawn);
	CHECK(welcome.rocks.size() == sim::AsteroidFieldDesc{}.count);

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

	// Firing: the shots show in snapshots, and Events tell of each.
	input.controls = {0.0f, 0.0f, true};
	const std::vector<u8> fire = sim::Write(input);
	REQUIRE(net::Send(client, fire.data(), u32(fire.size()), net::Delivery::Unreliable));
	for (int i = 0; i < 30; ++i)
		host.Update(1.0f / 60.0f);
	u32 fired = 0;
	while (net::Poll(client, event))
	{
		sim::Events events;
		if (sim::Read(event.data, event.size, events))
		{
			for (const sim::EventState& happened : events.events)
				fired += happened.type == sim::EventType::Fired && happened.ship == welcome.ship;
		}
		else
			sim::Read(event.data, event.size, *latest);
	}
	CHECK(fired >= 3);
	CHECK(latest->shotCount >= 3);

	// What went out and came in, by type, for the server's stats.
	const sim::MessageCounts& sent = host.GetSent();
	const sim::MessageCounts& received = host.GetReceived();
	CHECK(sent.count[u32(sim::MessageType::Welcome)] == 1);
	CHECK(sent.count[u32(sim::MessageType::Snapshot)] >= 44); // a tick each 1/30 s
	CHECK(sent.bytes[u32(sim::MessageType::Snapshot)] >=
	      sent.count[u32(sim::MessageType::Snapshot)] * 20);
	CHECK(received.count[u32(sim::MessageType::Hello)] == 1);
	CHECK(received.count[u32(sim::MessageType::Input)] == 2);
	CHECK(std::string(sim::MessageName(u32(sim::MessageType::Snapshot))) == "snapshot");

	// Leaving removes the ship.
	net::Close(client);
	host.Update(0.0f);
	CHECK(!sim::GetShip(*host.GetWorld(), sim::ShipFromId(welcome.ship)));
	host.Stop();
}

namespace
{
// A client of `host` at `address` that said Hello and read its Welcome.
sim::Welcome Join(server::Server& host, net::Client& client, const char* address)
{
	client = net::Connect(address);
	net::Event event;
	sim::Welcome welcome;
	if (!net::Poll(client, event) || event.type != net::EventType::Connected)
		return welcome;
	const std::vector<u8> hello = sim::Write(sim::Hello{});
	net::Send(client, hello.data(), u32(hello.size()), net::Delivery::Reliable);
	host.Update(0.0f);
	if (net::Poll(client, event))
		sim::Read(event.data, event.size, welcome);
	return welcome;
}

// Updates for `seconds` at 60 Hz; the client reads everything it got.
void Run(server::Server& host, net::Client client, f32 seconds,
         std::vector<sim::EventState>* events = nullptr)
{
	for (u32 i = 0; i < u32(seconds * 60.0f + 0.5f); ++i)
	{
		host.Update(1.0f / 60.0f);
		net::Event event;
		while (net::Poll(client, event))
		{
			sim::Events tick;
			if (events && sim::Read(event.data, event.size, tick))
				events->insert(events->end(), tick.events.begin(), tick.events.end());
		}
	}
}

u32 CountBots(const sim::World& world)
{
	u32 count = 0;
	for (u32 slot = 0; slot < sim::MAX_SHIPS; ++slot)
		count += world.shipIds[slot] && world.ships[slot].team == sim::BOTS;
	return count;
}
} // namespace

TEST_CASE("server: waves of bots come once a player is there, each bigger than the last")
{
	server::Server host;
	server::MatchDesc match;
	match.waveDelay = 0.5f;
	REQUIRE(host.Start("loopback:waves", match));
	host.Update(1.0f); // no players, no waves
	CHECK(host.GetWave() == 0);

	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:waves");
	REQUIRE(welcome.ship != 0);
	Run(host, client, 1.0f);
	CHECK(host.GetWave() == 1);
	sim::World& world = *host.GetWorld();
	CHECK(CountBots(world) == match.firstWave);
	// Away from the player, out of the rocks, and coming.
	for (u32 slot = 0; slot < sim::MAX_SHIPS; ++slot)
	{
		const sim::Ship& ship = world.ships[slot];
		if (!world.shipIds[slot] || ship.team != sim::BOTS)
			continue;
		CHECK(sim::Length(ship.position) > 80.0f);
		CHECK(ship.hull.health < sim::HullClass{}.health);
		for (u32 r = 0; r < world.rockCount; ++r)
			CHECK(sim::Length(ship.position - world.rocks[r].position) >=
			      world.rocks[r].radius + ship.hull.radius);
	}

	// Their wrecks go at once; the next wave, one bigger, after the delay.
	for (u32 slot = 0; slot < sim::MAX_SHIPS; ++slot)
	{
		if (world.shipIds[slot] && world.ships[slot].team == sim::BOTS)
			world.ships[slot].health = 0.0f;
	}
	Run(host, client, 0.1f);
	CHECK(CountBots(world) == 0);
	Run(host, client, 0.6f);
	CHECK(host.GetWave() == 2);
	CHECK(CountBots(world) == match.firstWave + 1);
	net::Close(client);
	host.Stop();
}

TEST_CASE("server: a dedicated server's match waits for players, and starts over without them")
{
	server::Server host;
	server::MatchDesc match;
	match.waveDelay = 0.5f;
	match.resetWhenEmpty = true;
	REQUIRE(host.Start("loopback:dedicated", match));
	host.Update(1.0f); // nobody: no ticks
	CHECK(host.GetWorld()->tick == 0);

	net::Client client;
	REQUIRE(Join(host, client, "loopback:dedicated").ship != 0);
	CHECK(host.GetPlayerCount() == 1);
	Run(host, client, 1.0f);
	CHECK(host.GetWorld()->tick >= 29);
	CHECK(host.GetWave() == 1);

	// The last one leaves: the match starts over, and waits again.
	net::Close(client);
	host.Update(0.0f);
	CHECK(host.GetPlayerCount() == 0);
	CHECK(host.GetWave() == 0);
	CHECK(CountBots(*host.GetWorld()) == 0);
	host.Update(1.0f);
	CHECK(host.GetWorld()->tick == 0);
	host.Stop();
}

TEST_CASE("server: a player's wreck comes back home, whole")
{
	server::Server host;
	server::MatchDesc match;
	match.bots = false;
	match.respawn = 1.0f;
	REQUIRE(host.Start("loopback:respawn", match));
	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:respawn");
	sim::Ship* ship = sim::GetShip(*host.GetWorld(), sim::ShipFromId(welcome.ship));
	REQUIRE(ship);
	ship->position = {50.0f, 50.0f};
	ship->health = 0.0f;
	Run(host, client, 0.5f);
	CHECK(!sim::IsAlive(*ship));
	CHECK(ship->position.x == 50.0f); // a wreck stays put
	Run(host, client, 0.7f);
	CHECK(sim::IsAlive(*ship));
	CHECK(ship->health == ship->hull.health);
	CHECK(ship->shield == ship->hull.shield);
	CHECK(sim::Length(ship->position) < 5.0f);
	net::Close(client);
	host.Stop();
}

TEST_CASE("server: bots attack a player who holds still")
{
	server::Server host;
	server::MatchDesc match;
	match.waveDelay = 0.5f;
	REQUIRE(host.Start("loopback:attack", match));
	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:attack");
	std::vector<sim::EventState> events;
	Run(host, client, 20.0f, &events);
	u32 hits = 0;
	u32 shots = 0;
	for (const sim::EventState& event : events)
	{
		hits +=
			(event.type == sim::EventType::ShieldHit || event.type == sim::EventType::HullHit) &&
			event.ship == welcome.ship && event.other != 0;
		shots += event.type == sim::EventType::Fired && event.ship != welcome.ship;
	}
	MESSAGE("bots fired ", shots, " shots, ", hits, " hit the player");
	CHECK(shots >= 5);
	CHECK(hits > 0);
	net::Close(client);
	host.Stop();
}

TEST_CASE("server: a player on autopilot fights the waves")
{
	server::Server host;
	server::MatchDesc match;
	match.waveDelay = 1.0f;
	match.autopilot = true;
	REQUIRE(host.Start("loopback:autopilot", match));
	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:autopilot");
	std::vector<sim::EventState> events;
	Run(host, client, 60.0f, &events);
	u32 kills = 0;
	u32 losses = 0;
	u32 fired = 0;
	u32 hits = 0;
	u32 enemyFired = 0;
	u32 enemyHits = 0;
	for (const sim::EventState& event : events)
	{
		const bool shield = event.type == sim::EventType::ShieldHit;
		const bool hull = event.type == sim::EventType::HullHit;
		kills += event.type == sim::EventType::ShipDestroyed && event.other == welcome.ship;
		losses += event.type == sim::EventType::ShipDestroyed && event.ship == welcome.ship;
		fired += event.type == sim::EventType::Fired && event.ship == welcome.ship;
		enemyFired += event.type == sim::EventType::Fired && event.ship != welcome.ship;
		hits += (shield || hull) && event.other == welcome.ship;
		enemyHits += (shield || hull) && event.ship == welcome.ship;
	}
	MESSAGE("autopilot: ", fired, " shots, ", hits, " hits, ", kills, " kills, ", losses,
	        " losses; bots: ", enemyFired, " shots, ", enemyHits, " hits; wave ", host.GetWave());
	CHECK(kills >= 2);
	net::Close(client);
	host.Stop();
}
