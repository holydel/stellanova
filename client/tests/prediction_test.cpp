#include "prediction.h"

#include <sn/server/server.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <doctest/doctest.h>

#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <vector>

using namespace sn;
using namespace ph;

TEST_CASE("prediction: the controls the server has not applied fly again from its word")
{
	Prediction prediction;
	prediction.Reset(sim::HullClass{}, sim::WeaponClass{}, {});
	sim::Shot shot;
	// Before the server's first word, controls are only kept.
	for (u32 i = 0; i < 3; ++i)
		CHECK(!prediction.Step(sim::Quantize({0.5f, 1.0f, false}), shot));
	CHECK(!prediction.IsActive());
	REQUIRE(prediction.Pending().size() == 3);
	CHECK(prediction.Pending().back().number == 3);

	// It applied the first: the other two fly from where it says the ship is.
	sim::ShipState state;
	state.position = {1.0f, 2.0f};
	state.health = 255;
	prediction.Correct(state, 0.0f, 1);
	CHECK(prediction.IsActive());
	CHECK(prediction.Pending().size() == 2);
	sim::Ship alone;
	alone.position = {1.0f, 2.0f};
	alone.controls = sim::Quantize({0.5f, 1.0f, false});
	sim::FlyShip(alone);
	const Vec2 before = alone.position;
	sim::FlyShip(alone);
	CHECK(prediction.Now().position.x == alone.position.x);
	CHECK(prediction.Now().position.y == alone.position.y);
	CHECK(prediction.Before().position.y == before.y);

	// A wreck is not predicted.
	state.health = 0;
	prediction.Correct(state, 0.0f, 3);
	CHECK(!prediction.IsActive());
	CHECK(prediction.Pending().empty());
}

namespace
{
// What the client sends at its tick `tick`: turns and thrusts that change
// every second, the trigger now and then.
sim::ShipControls Script(u32 tick, bool fire)
{
	const u32 second = tick / 30;
	sim::ShipControls controls;
	controls.thrust = second % 3 == 2 ? -0.6f : 1.0f;
	controls.turn = second < 2 ? 0.0f : std::sin(f32(tick) * 0.07f);
	controls.fire = fire && (tick / 10) % 3 != 0;
	return sim::Quantize(controls);
}

// A client and a server over loopback, each way `delay` ticks late: the
// server ticks once for each of the client's ticks. Returns how many of the
// client's predictions matched the server's ship after the same controls,
// and checks every one; `fires` counts the shots both made at once.
u32 Fly(const char* address, u32 delay, bool rock, bool fire, u32& fires)
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start(address, match));
	sim::World& world = *host.GetWorld();
	world.rockCount = 0;
	if (rock)
	{
		// Right in the way: the ship starts at the origin facing +y.
		world.rocks[0] = {{0.0f, 32.0f}, 4.0f, 100.0f};
		world.rockCount = 1;
	}

	const net::Client client = net::Connect(address);
	net::Event event;
	REQUIRE(net::Poll(client, event));
	const std::vector<u8> hello = sim::Write(sim::Hello{});
	net::Send(client, hello.data(), u32(hello.size()), net::Delivery::Reliable);
	host.Update(0.0f);
	sim::Welcome welcome;
	while (net::Poll(client, event))
		sim::Read(event.data, event.size, welcome);
	REQUIRE(welcome.ship != 0);
	Prediction prediction;
	prediction.Reset(welcome.hull, welcome.weapon, welcome.rocks);

	struct Due
	{
		u32 tick = 0;
		std::vector<u8> bytes;
	};
	std::deque<Due> uplink;
	std::deque<Due> downlink;
	std::map<u32, Vec2> predicted; // by the controls' number
	std::map<u32, bool> predictedFire;
	std::map<u32, Vec2> served;
	std::map<u32, bool> servedFire;
	auto snapshot = std::make_unique<sim::Snapshot>();
	bool firedThisTick = false;
	for (u32 tick = 1; tick <= 300; ++tick)
	{
		// The client's tick: flown at once, sent late.
		sim::Shot shot;
		const bool shoots = prediction.Step(Script(tick, fire), shot);
		const u32 number = prediction.Pending().back().number;
		if (prediction.IsActive())
		{
			predicted[number] = prediction.Now().position;
			predictedFire[number] = shoots;
		}
		sim::Input input;
		input.count = std::min(u32(prediction.Pending().size()), sim::MAX_INPUTS);
		input.last = number;
		for (u32 i = 0; i < input.count; ++i)
			input.controls[i] =
				prediction.Pending()[prediction.Pending().size() - input.count + i].controls;
		uplink.push_back({tick + delay, sim::Write(input)});

		// What is due reaches the server, which ticks once.
		while (!uplink.empty() && uplink.front().tick <= tick)
		{
			const std::vector<u8>& bytes = uplink.front().bytes;
			net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Unreliable);
			uplink.pop_front();
		}
		host.Update(sim::TICK_SECONDS);

		// Its word on our ship after each set of controls; sent back late.
		while (net::Poll(client, event))
		{
			sim::Events events;
			if (sim::Read(event.data, event.size, events))
			{
				for (const sim::EventState& happened : events.events)
					firedThisTick |=
						happened.type == sim::EventType::Fired && happened.ship == welcome.ship;
				continue;
			}
			if (!sim::Read(event.data, event.size, *snapshot))
				continue;
			REQUIRE(snapshot->count > 0);
			CHECK(snapshot->ships[0].id == welcome.ship);
			if (snapshot->input && !served.count(snapshot->input))
			{
				served[snapshot->input] = snapshot->ships[0].position;
				servedFire[snapshot->input] = firedThisTick;
			}
			firedThisTick = false;
			downlink.push_back(
				{tick + delay, std::vector<u8>(event.data, event.data + event.size)});
		}
		while (!downlink.empty() && downlink.front().tick <= tick)
		{
			const std::vector<u8>& bytes = downlink.front().bytes;
			REQUIRE(sim::Read(bytes.data(), bytes.size(), *snapshot));
			prediction.Correct(snapshot->ships[0], snapshot->cooldown, snapshot->input);
			downlink.pop_front();
		}
	}

	u32 matched = 0;
	for (const auto& [number, position] : predicted)
	{
		const auto server = served.find(number);
		if (server == served.end())
			continue;
		CHECK(position.x == server->second.x);
		CHECK(position.y == server->second.y);
		CHECK(predictedFire[number] == servedFire[number]);
		fires += predictedFire[number] && servedFire[number];
		++matched;
	}
	net::Close(client);
	host.Stop();
	return matched;
}
} // namespace

TEST_CASE("prediction: our ship is where the server will have it, rocks and all")
{
	u32 fires = 0;
	// 100 ms each way: the ship runs into the rock in its way and bounces.
	CHECK(Fly("loopback:prediction-rock", 3, true, false, fires) >= 250);
	// The gun fires on the same ticks as the server's.
	CHECK(Fly("loopback:prediction-gun", 5, false, true, fires) >= 250);
	CHECK(fires >= 30);
}
