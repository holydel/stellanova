#pragma once

#include <sn/bots/pilot.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <memory>
#include <vector>

// A match's authority (docs/adr/0007-local-server.md, 0009-bots-v0.md): it
// listens at an address, gives each client that says Hello a ship, applies
// their inputs, flies the match's bots, runs the sim at its fixed tick, and
// sends every client the tick's events and a snapshot after each tick. A
// skirmish sends waves of enemy bots, each one bigger, and brings destroyed
// players back. No globals: a client can host one in its own process.
namespace sn::server
{
// A match's rules.
struct MatchDesc
{
	sim::AsteroidFieldDesc field;
	bool bots = true;         // waves of enemies
	ph::u32 firstWave = 2;    // bots in the first wave; each next one has one more
	ph::u32 largestWave = 8;  // bots at most
	ph::f32 waveDelay = 4.0f; // s before each wave: at the start, and once a wave is gone
	ph::f32 respawn = 3.0f;   // s from a player's wreck to its ship's return
	ph::u32 seed = 1;         // where the waves come from
	bool autopilot = false;   // players' ships fly themselves, as bots do (demos, tests)
	// When the last player leaves, the match starts over: a new field, the
	// first wave (a dedicated server between visitors). Until someone comes,
	// it waits: no ticks for nobody.
	bool resetWhenEmpty = false;
};

class Server
{
public:
	// "loopback:<name>" for a local game. False when the address is taken.
	bool Start(const char* address, const MatchDesc& match = {});
	void Stop();
	bool IsRunning() const { return bool(listener); }
	// What the clients said, then the ticks that `dt` seconds cover.
	void Update(ph::f32 dt);
	const sim::World* GetWorld() const { return world.get(); }
	// For tests and tools: the world, to change between updates.
	sim::World* GetWorld() { return world.get(); }
	// The enemies' wave: 0 before the first.
	ph::u32 GetWave() const { return wave; }
	ph::u32 GetPlayerCount() const { return ph::u32(players.size()); }
	// A player's peer, for its traffic (net::GetPeerStats).
	ph::net::PeerId GetPlayerPeer(ph::u32 index) const { return players[index].peer; }
	ph::net::Server GetListener() const { return listener; }
	// Since Start: the messages that went out (one per player sent to) and
	// came in, by type.
	const sim::MessageCounts& GetSent() const { return sent; }
	const sim::MessageCounts& GetReceived() const { return received; }

private:
	struct Player
	{
		ph::net::PeerId peer = 0;
		sim::ShipHandle ship;
		ph::Vec2 home;       // where its ship starts, and comes back
		ph::f32 down = 0.0f; // s its ship has been a wreck
		bots::Pilot pilot;   // with MatchDesc::autopilot
	};

	// A bot of the enemies' wave.
	struct Enemy
	{
		sim::ShipHandle ship;
		bots::Pilot pilot;
	};

	void OnMessage(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void Leave(ph::net::PeerId peer);
	// The match from its start: the field made again, no bots, no wave.
	void Restart();
	// After a step: wrecked bots go, wrecked players come back after a
	// while, and the next wave comes once the last one is gone.
	void Referee();
	void SendWave();
	// A point near `near` where a circle of `radius` touches no rock.
	ph::Vec2 FindRoom(ph::Vec2 near, ph::f32 radius) const;
	ph::f32 Random();
	// After each tick: what happened (reliable), then where things are.
	void SendEvents();
	void SendSnapshots();
	// Sends and counts.
	void Send(ph::net::PeerId peer, const std::vector<ph::u8>& bytes, ph::net::Delivery delivery);

	ph::net::Server listener;
	std::unique_ptr<sim::World> world;
	MatchDesc match;
	std::vector<Player> players;
	std::vector<Enemy> enemies;
	bots::PilotSkill skill; // the wave's
	ph::u32 wave = 0;
	ph::f32 untilWave = 0.0f; // s, counted while no wave is out
	ph::u32 seed = 1;
	ph::f32 sinceTick = 0.0f;
	sim::Snapshot snapshot; // reused each tick
	sim::Events events;
	sim::MessageCounts sent;
	sim::MessageCounts received;
};
} // namespace sn::server
