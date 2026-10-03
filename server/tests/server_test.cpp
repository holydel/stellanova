#include <sn/server/server.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

using namespace sn;
using namespace ph;

TEST_CASE("protocol: messages read back, and refuse other bytes")
{
	sim::Input input;
	input.last = 42;
	input.count = 2;
	input.controls[0] = {-0.5f, 1.0f};
	input.controls[1] = {0.25f, -1.0f, true};
	std::vector<u8> bytes = sim::Write(input);
	CHECK(sim::TypeOf(bytes.data(), bytes.size()) == sim::MessageType::Input);
	sim::Input back;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), back));
	CHECK(back.last == 42);
	CHECK(back.count == 2);
	// Controls travel in 1/127 steps: what arrives is what Quantize says.
	CHECK(back.controls[0].turn == sim::Quantize({-0.5f, 1.0f}).turn);
	CHECK(back.controls[0].thrust == 1.0f);
	CHECK(back.controls[1].thrust == -1.0f);
	CHECK(back.controls[1].fire);
	sim::Welcome welcome;
	CHECK(!sim::Read(bytes.data(), bytes.size(), welcome));  // another type
	CHECK(!sim::Read(bytes.data(), bytes.size() - 1, back)); // cut short
	// No controls, too many, or numbers before the first: refused.
	for (const u8 count : {u8(0), u8(sim::MAX_INPUTS + 1)})
	{
		std::vector<u8> wrong = bytes;
		wrong[1 + 4] = count;
		CHECK(!sim::Read(wrong.data(), wrong.size(), back));
	}
	input.last = 1;
	bytes = sim::Write(input);
	CHECK(!sim::Read(bytes.data(), bytes.size(), back));
	// Nothing on the wire can be NaN or out of range.
	const sim::ShipControls odd =
		sim::Quantize({std::numeric_limits<f32>::quiet_NaN(), 7.0f, false});
	CHECK(odd.turn == 0.0f);
	CHECK(odd.thrust == 1.0f);

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
	snapshot->input = 17;
	snapshot->cooldown = 0.08f;
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
	CHECK(read->input == 17);
	CHECK(read->cooldown == 0.08f);
	REQUIRE(read->shotCount == 1);
	CHECK(read->shots[0].team == sim::BOTS);
	// A sliver of health is not a wreck.
	CHECK(sim::Share(0.001f, 12.0f) == 1);
	CHECK(sim::Share(0.0f, 12.0f) == 0);

	// A float that is not finite refuses the whole message.
	std::vector<u8> poisoned = snapshotBytes;
	const f32 nan = std::numeric_limits<f32>::quiet_NaN();
	const usize angle = 1 + 8 + 4 + 4 + 4 + 4 + 4 + 8 + 8; // the first ship's angle
	std::memcpy(poisoned.data() + angle, &nan, sizeof(nan));
	CHECK(!sim::Read(poisoned.data(), poisoned.size(), *read));

	// Why a server turns a client away.
	sim::Refusal refusal;
	refusal.reason = sim::RefusalReason::Full;
	bytes = sim::Write(refusal);
	sim::Refusal refusalBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), refusalBack));
	CHECK(refusalBack.reason == sim::RefusalReason::Full);
	CHECK(refusalBack.version == sim::PROTOCOL_VERSION);
	bytes[1] = 9; // no such reason
	CHECK(!sim::Read(bytes.data(), bytes.size(), refusalBack));
}

TEST_CASE("protocol: the field, shots and events read back")
{
	sim::Welcome welcome;
	welcome.ship = 7;
	welcome.respawn = 2.5f;
	welcome.hull.radius = 2.5f;
	welcome.weapon.interval = 0.2f;
	welcome.autopilot = true;
	welcome.rocks = {{{1.0f, 2.0f}, 3.0f, 3.0f}, {{-5.0f, 6.0f}, 1.5f, 0.0f}};
	std::vector<u8> bytes = sim::Write(welcome);
	sim::Welcome welcomeBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), welcomeBack));
	CHECK(welcomeBack.respawn == 2.5f);
	CHECK(welcomeBack.hull.radius == 2.5f);
	CHECK(welcomeBack.hull.maxSpeed == sim::HullClass{}.maxSpeed);
	CHECK(welcomeBack.weapon.interval == 0.2f);
	CHECK(welcomeBack.autopilot);
	REQUIRE(welcomeBack.rocks.size() == 2);
	CHECK(welcomeBack.rocks[1].position.x == -5.0f);
	CHECK(welcomeBack.rocks[1].health == 0.0f);

	sim::Input input;
	input.last = 1;
	input.count = 1;
	input.controls[0] = {0.0f, 1.0f, true};
	bytes = sim::Write(input);
	sim::Input inputBack;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), inputBack));
	CHECK(inputBack.controls[0].fire);
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
	match.waves = false;
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

	CHECK(welcome.hull.maxSpeed == sim::HullClass{}.maxSpeed);
	CHECK(!welcome.autopilot);

	// Full thrust for a second: 30 ticks, a snapshot after each, and our
	// controls for each, as a client sends them.
	sim::Input input;
	input.count = 1;
	input.controls[0].thrust = 1.0f;
	for (int i = 0; i < 61; ++i)
	{
		if (i % 2 == 0)
		{
			++input.last;
			const std::vector<u8> bytes = sim::Write(input);
			REQUIRE(net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Unreliable));
		}
		host.Update(1.0f / 60.0f);
	}
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
	CHECK(latest->input > 25); // our controls, applied as they came
	CHECK(latest->ships[0].id == welcome.ship);

	// Firing: Events tell of each shot. Our own shots stay out of our
	// snapshots: our client draws them from its prediction.
	++input.last;
	input.controls[0] = {0.0f, 0.0f, true};
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
	CHECK(latest->shotCount == 0);
	CHECK(latest->input == input.last);
	u32 flying = 0;
	for (const sim::Shot& shot : host.GetWorld()->shots)
		flying += shot.life > 0.0f;
	CHECK(flying >= 3);

	// What went out and came in, by type, for the server's stats.
	const sim::MessageCounts& sent = host.GetSent();
	const sim::MessageCounts& received = host.GetReceived();
	CHECK(sent.count[u32(sim::MessageType::Welcome)] == 1);
	CHECK(sent.count[u32(sim::MessageType::Snapshot)] >= 44); // a tick each 1/30 s
	CHECK(sent.bytes[u32(sim::MessageType::Snapshot)] >=
	      sent.count[u32(sim::MessageType::Snapshot)] * 20);
	CHECK(received.count[u32(sim::MessageType::Hello)] == 1);
	CHECK(received.count[u32(sim::MessageType::Input)] == input.last);
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

namespace
{
// What a client heard of the chat since the last call.
std::vector<sim::Chat> ChatHeard(net::Client client)
{
	std::vector<sim::Chat> lines;
	net::Event event;
	sim::Chat chat;
	while (net::Poll(client, event))
	{
		if (sim::Read(event.data, event.size, chat))
			lines.push_back(chat);
	}
	return lines;
}

void Say(server::Server& host, net::Client client, const char* text)
{
	const std::vector<u8> bytes = sim::Write(sim::Say{text});
	net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Reliable);
	host.Update(0.0f);
}
} // namespace

TEST_CASE("server: players chat, and call in bots and send them away")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:chat", match));
	net::Client ann;
	net::Client bo;
	const sim::Welcome annWelcome = Join(host, ann, "loopback:chat");
	const std::vector<u8> name = sim::Write(sim::Name{"  Ann\n"});
	net::Send(ann, name.data(), u32(name.size()), net::Delivery::Reliable);
	host.Update(1.0f);
	Join(host, bo, "loopback:chat");
	ChatHeard(ann);
	ChatHeard(bo);

	// A line reaches everyone, with its speaker; a name is cleaned.
	Say(host, ann, "hello\x01 there ");
	std::vector<sim::Chat> heard = ChatHeard(bo);
	REQUIRE(heard.size() == 1);
	CHECK(!heard[0].notice);
	CHECK(heard[0].ship == annWelcome.ship);
	CHECK(heard[0].name == "Ann");
	CHECK(heard[0].text == "hello there");
	CHECK(ChatHeard(ann).size() == 1);

	// Bots come on command, near the one who asked, and go.
	Say(host, ann, "/add_bots 4");
	CHECK(host.GetBotCount() == 4);
	heard = ChatHeard(bo);
	REQUIRE(heard.size() == 1);
	CHECK(heard[0].notice);
	CHECK(heard[0].text == "chat.bots_added");
	CHECK(heard[0].name == "Ann");
	CHECK(heard[0].extra == "4");
	Say(host, ann, "/remove_bots");
	CHECK(host.GetBotCount() == 0);
	Say(host, ann, "/add_bots 100"); // as many as snapshots carry
	CHECK(host.GetBotCount() == sim::MAX_SNAPSHOT_SHIPS - 2);
	Say(host, ann, "/remove_bots");

	// Only the asker hears an unknown command; a flood is held back.
	ChatHeard(ann);
	ChatHeard(bo);
	Say(host, bo, "/fly_me_to_the_moon");
	heard = ChatHeard(bo);
	REQUIRE(heard.size() == 1);
	CHECK(heard[0].text == "chat.unknown");
	CHECK(ChatHeard(ann).empty());
	for (int i = 0; i < 8; ++i)
		Say(host, bo, "spam");
	heard = ChatHeard(bo);
	CHECK(std::count_if(heard.begin(), heard.end(),
	                    [](const sim::Chat& chat) { return chat.text == "chat.too_fast"; }) >= 3);

	// Leaving is told.
	net::Close(bo);
	host.Update(0.0f);
	heard = ChatHeard(ann);
	REQUIRE(!heard.empty());
	CHECK(heard.back().text == "chat.left");
	net::Close(ann);
	host.Stop();
}

TEST_CASE("protocol: chat text is cleaned and cut between characters")
{
	CHECK(sim::CleanText("  hi  ", 10) == "hi");
	CHECK(sim::CleanText("a\tb\x7f"
	                     "c",
	                     10) == "abc");
	CHECK(sim::CleanText("\xff\xfe ok", 10) == "ok");
	CHECK(sim::CleanText("\xd0\x9f\xd1\x80\xd0\xb8", 5) ==
	      "\xd0\x9f\xd1\x80");                         // "При", cut to "Пр"
	CHECK(sim::CleanText("\xed\xa0\x80x", 10) == "x"); // a lone surrogate
	sim::Chat chat;
	chat.ship = 7;
	chat.notice = true;
	chat.name = "Ann";
	chat.text = "chat.joined";
	const std::vector<u8> bytes = sim::Write(chat);
	sim::Chat back;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), back));
	CHECK(back.ship == 7);
	CHECK(back.notice);
	CHECK(back.name == "Ann");
	CHECK(back.text == "chat.joined");
	CHECK(back.extra.empty());
	const std::vector<u8> tooLong = sim::Write(sim::Say{std::string(sim::MAX_CHAT_BYTES + 1, 'x')});
	sim::Say say;
	CHECK(!sim::Read(tooLong.data(), tooLong.size(), say));
}

namespace
{
// A client's next message of that type, polling until it comes or the
// updates (each of dt seconds) pass; false if it never did.
bool Await(server::Server& host, net::Client client, sim::MessageType type, std::vector<u8>& bytes,
           u32 updates = 4, f32 dt = 0.0f)
{
	for (u32 i = 0; i <= updates; ++i)
	{
		net::Event event;
		while (net::Poll(client, event))
		{
			if (event.type == net::EventType::Message &&
			    sim::TypeOf(event.data, event.size) == type)
			{
				bytes.assign(event.data, event.data + event.size);
				return true;
			}
		}
		host.Update(dt);
	}
	return false;
}

// Whether the server let the client go within that many seconds of updates.
bool LetGo(server::Server& host, net::Client client, f32 seconds)
{
	for (u32 i = 0; i < u32(seconds * 10.0f + 0.5f); ++i)
	{
		host.Update(0.1f);
		net::Event event;
		while (net::Poll(client, event))
		{
			if (event.type == net::EventType::Disconnected)
				return true;
		}
	}
	return false;
}

// A client of that address, connected (at once, over loopback).
net::Client Connected(const char* address)
{
	const net::Client client = net::Connect(address);
	net::Event event;
	net::Poll(client, event);
	return client;
}

void SendInput(net::Client client, u32 last, u32 count, sim::ShipControls controls)
{
	sim::Input input;
	input.last = last;
	input.count = count;
	for (u32 i = 0; i < count; ++i)
		input.controls[i] = controls;
	const std::vector<u8> bytes = sim::Write(input);
	net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Unreliable);
}
} // namespace

TEST_CASE("server: a second Hello changes nothing")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:twice", match));
	net::Client client;
	REQUIRE(Join(host, client, "loopback:twice").ship != 0);
	const std::vector<u8> hello = sim::Write(sim::Hello{});
	net::Send(client, hello.data(), u32(hello.size()), net::Delivery::Reliable);
	host.Update(0.0f);
	CHECK(host.GetPlayerCount() == 1);
	// Still in the match: snapshots keep coming.
	std::vector<u8> bytes;
	CHECK(Await(host, client, sim::MessageType::Snapshot, bytes, 4, sim::TICK_SECONDS));
	CHECK(!LetGo(host, client, 1.0f));
	// Leaving still ends it.
	net::Close(client);
	host.Update(0.0f);
	CHECK(host.GetPlayerCount() == 0);
	host.Stop();
}

TEST_CASE("server: another version, or one player too many, is told why and let go")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	match.maxPlayers = 1;
	REQUIRE(host.Start("loopback:refusals", match));
	net::Client first;
	REQUIRE(Join(host, first, "loopback:refusals").ship != 0);

	const net::Client late = Connected("loopback:refusals");
	const std::vector<u8> hello = sim::Write(sim::Hello{});
	net::Send(late, hello.data(), u32(hello.size()), net::Delivery::Reliable);
	std::vector<u8> bytes;
	REQUIRE(Await(host, late, sim::MessageType::Refusal, bytes));
	sim::Refusal refusal;
	REQUIRE(sim::Read(bytes.data(), bytes.size(), refusal));
	CHECK(refusal.reason == sim::RefusalReason::Full);
	CHECK(host.GetPlayerCount() == 1);
	CHECK(LetGo(host, late, 2.0f));

	const net::Client old = Connected("loopback:refusals");
	const std::vector<u8> oldHello = sim::Write(sim::Hello{3});
	net::Send(old, oldHello.data(), u32(oldHello.size()), net::Delivery::Reliable);
	REQUIRE(Await(host, old, sim::MessageType::Refusal, bytes));
	REQUIRE(sim::Read(bytes.data(), bytes.size(), refusal));
	CHECK(refusal.reason == sim::RefusalReason::Version);
	CHECK(refusal.version == sim::PROTOCOL_VERSION);
	CHECK(LetGo(host, old, 2.0f));
	net::Close(first);
	host.Stop();
}

TEST_CASE("server: a peer that never says Hello is let go")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:silent", match));
	const net::Client silent = net::Connect("loopback:silent");
	CHECK(!LetGo(host, silent, 5.0f));
	CHECK(LetGo(host, silent, 6.0f));
	CHECK(host.GetPlayerCount() == 0);
	host.Stop();
}

TEST_CASE("server: controls apply one set a tick, and each snapshot says which")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:controls", match));
	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:controls");
	REQUIRE(welcome.ship != 0);
	std::vector<u8> bytes;
	auto snapshot = std::make_unique<sim::Snapshot>();

	// One set: applied at the next tick, then held.
	SendInput(client, 1, 1, {0.0f, 1.0f});
	REQUIRE(Await(host, client, sim::MessageType::Snapshot, bytes, 4, sim::TICK_SECONDS));
	REQUIRE(sim::Read(bytes.data(), bytes.size(), *snapshot));
	CHECK(snapshot->input == 1);
	CHECK(snapshot->ships[0].controls.thrust == 1.0f);

	// Repeats are applied once; three sets go at one a tick.
	SendInput(client, 4, 4, {0.5f, 1.0f});
	u32 seen[3] = {};
	for (u32& number : seen)
	{
		REQUIRE(Await(host, client, sim::MessageType::Snapshot, bytes, 4, sim::TICK_SECONDS));
		REQUIRE(sim::Read(bytes.data(), bytes.size(), *snapshot));
		number = snapshot->input;
	}
	CHECK(seen[0] == 2);
	CHECK(seen[1] == 3);
	CHECK(seen[2] == 4);

	// Far ahead (a burst after a stall): the oldest go, a few stay.
	SendInput(client, 12, 8, {0.0f, 0.0f});
	REQUIRE(Await(host, client, sim::MessageType::Snapshot, bytes, 4, sim::TICK_SECONDS));
	REQUIRE(sim::Read(bytes.data(), bytes.size(), *snapshot));
	CHECK(snapshot->input == 10);

	// Controls that stop coming let go of the stick after a moment.
	SendInput(client, 13, 1, {0.0f, 1.0f});
	host.Update(sim::TICK_SECONDS);
	const sim::Ship* flown = sim::GetShip(*host.GetWorld(), sim::ShipFromId(welcome.ship));
	REQUIRE(flown);
	for (u32 i = 0; i < 5; ++i)
		host.Update(sim::TICK_SECONDS);
	CHECK(flown->controls.thrust == 1.0f);
	for (u32 i = 0; i < 15; ++i)
		host.Update(sim::TICK_SECONDS);
	CHECK(flown->controls.thrust == 0.0f);
	net::Close(client);
	host.Stop();
}

TEST_CASE("server: each player's snapshot starts with its own ship, then the nearest")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:crowd", match));
	// More ships than a snapshot holds, far and near.
	sim::World& world = *host.GetWorld();
	for (u32 i = 0; i < 40; ++i)
	{
		sim::Ship bot;
		bot.team = sim::BOTS;
		bot.position = {500.0f + f32(i), 500.0f};
		sim::SpawnShip(world, bot);
	}
	sim::Ship near;
	near.team = sim::BOTS;
	near.position = {0.0f, 25.0f};
	const sim::ShipHandle nearby = sim::SpawnShip(world, near);

	net::Client client;
	const sim::Welcome welcome = Join(host, client, "loopback:crowd");
	REQUIRE(welcome.ship != 0);
	std::vector<u8> bytes;
	REQUIRE(Await(host, client, sim::MessageType::Snapshot, bytes, 4, sim::TICK_SECONDS));
	auto snapshot = std::make_unique<sim::Snapshot>();
	REQUIRE(sim::Read(bytes.data(), bytes.size(), *snapshot));
	CHECK(snapshot->count == sim::MAX_SNAPSHOT_SHIPS);
	CHECK(snapshot->ships[0].id == welcome.ship);
	CHECK(snapshot->ships[1].id == sim::ShipId(nearby));
	net::Close(client);
	host.Stop();
}

TEST_CASE(
	"server: names are unique, a new name is told, and /waves lasts until the match starts over")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	match.resetWhenEmpty = true;
	match.waveDelay = 0.2f;
	REQUIRE(host.Start("loopback:names", match));
	net::Client ann;
	net::Client other;
	Join(host, ann, "loopback:names");
	Join(host, other, "loopback:names");
	// Both take "Ann": the second gets a number.
	for (const net::Client client : {ann, other})
	{
		const std::vector<u8> name = sim::Write(sim::Name{"Ann"});
		net::Send(client, name.data(), u32(name.size()), net::Delivery::Reliable);
		host.Update(0.0f);
	}
	ChatHeard(ann);
	Say(host, other, "/who");
	const std::vector<sim::Chat> heard = ChatHeard(other);
	REQUIRE(!heard.empty());
	CHECK(heard.back().text == "chat.who");
	CHECK(heard.back().extra == "Ann, Ann 2");
	// A rename after joining is told to everyone.
	const auto renamed =
		std::find_if(heard.begin(), heard.end(), [](const sim::Chat& chat)
		             { return chat.text == "chat.renamed" && chat.name == "Pilot 2"; });
	REQUIRE(renamed != heard.end());
	CHECK(renamed->extra == "Ann 2");
	// Invisible characters do not make another name.
	CHECK(sim::CleanText("A\xe2\x80\x8bnn", sim::MAX_NAME_BYTES) == "Ann");
	CHECK(sim::CleanText("\xe2\x80\xae"
	                     "Ann",
	                     sim::MAX_NAME_BYTES) == "Ann");
	CHECK(sim::CleanText("\xc2\xa0"
	                     "Ann\xe3\x80\x80\xe3\x80\x80"
	                     "Bo ",
	                     16) == "Ann Bo");

	// Waves on, everyone gone: the next match is quiet again.
	Say(host, ann, "/waves on");
	net::Close(ann);
	net::Close(other);
	host.Update(0.0f);
	CHECK(host.GetPlayerCount() == 0);
	net::Client next;
	REQUIRE(Join(host, next, "loopback:names").ship != 0);
	Run(host, next, 1.0f);
	CHECK(host.GetWave() == 0);
	net::Close(next);
	host.Stop();
}

TEST_CASE("server: a player's wreck comes back home, whole")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
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
