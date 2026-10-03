#pragma once

#include <sn/bots/pilot.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A match's authority (docs/adr/0007-local-server.md, 0009-bots-v0.md): it
// listens at an address, gives each client that says Hello a ship, applies
// their inputs, flies the match's bots, runs the sim at its fixed tick, and
// sends every client the tick's events and a snapshot after each tick. A
// skirmish sends waves of enemy bots, each one bigger, and brings destroyed
// players back. Players chat, and call in bots with commands (/help lists
// them). No globals: a client can host one in its own process.
namespace sn::server
{
// A match's rules.
struct MatchDesc
{
	sim::AsteroidFieldDesc field;
	bool waves = true;        // waves of enemy bots (/waves on, /waves off)
	ph::u32 firstWave = 2;    // bots in the first wave; each next one has one more
	ph::u32 largestWave = 8;  // bots at most
	ph::f32 waveDelay = 4.0f; // s before each wave: at the start, and once a wave is gone
	ph::f32 respawn = 3.0f;   // s from a player's wreck to its ship's return
	ph::u32 seed = 1;         // where the waves come from
	bool autopilot = false;   // players' ships fly themselves, as bots do (demos, tests)
	// When the last player leaves, the match starts over: a new field, the
	// first wave, the waves as they were set here (a dedicated server between
	// visitors). Until someone comes, it waits: no ticks for nobody.
	bool resetWhenEmpty = false;
	ph::u32 maxPlayers = 16; // more are refused (sim::RefusalReason::Full)
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
	ph::u32 GetBotCount() const { return ph::u32(enemies.size()); }
	// A player's peer, for its traffic (net::GetPeerStats).
	ph::net::PeerId GetPlayerPeer(ph::u32 index) const { return players[index].peer; }
	ph::net::Server GetListener() const { return listener; }
	// Since Start: the messages that went out (one per player sent to) and
	// came in, by type.
	const sim::MessageCounts& GetSent() const { return sent; }
	const sim::MessageCounts& GetReceived() const { return received; }

private:
	// A tick's controls from a player, by its number (sim::Input).
	struct Controls
	{
		ph::u32 number = 0;
		sim::ShipControls controls;
	};

	struct Player
	{
		ph::net::PeerId peer = 0;
		sim::ShipHandle ship;
		ph::Vec2 home;            // where its ship starts, and comes back
		ph::f32 down = 0.0f;      // s its ship has been a wreck
		bots::Pilot pilot;        // with MatchDesc::autopilot
		std::string name;         // its Name, else "Pilot <n>", unique in the match
		bool announced = false;   // "joined" went out
		ph::f32 chatLines = 5.0f; // lines it may say now: one more a second, five at most
		// Its controls not yet applied, oldest first: one set each tick.
		std::vector<Controls> queued;
		ph::u32 received = 0; // the newest number that came
		ph::u32 applied = 0;  // the last number applied: its snapshots say so
		ph::u32 idle = 0;     // ticks since the last set came
	};

	// A peer that connected and has not joined: it must say Hello soon. A
	// refused one waits a moment for its Refusal to go out.
	struct Newcomer
	{
		ph::net::PeerId peer = 0;
		ph::f32 age = 0.0f; // s
		bool refused = false;
	};

	// A bot of the enemies' wave.
	struct Enemy
	{
		sim::ShipHandle ship;
		bots::Pilot pilot;
	};

	void OnMessage(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void OnHello(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void OnName(Player& player, std::string_view name);
	// Why not, then a moment later, the door.
	void Refuse(ph::net::PeerId peer, sim::RefusalReason reason);
	// Newcomers that never said Hello, and refused ones, are disconnected.
	void Admit(ph::f32 dt);
	// Each player's next controls, for this tick.
	void ApplyControls();
	void Leave(ph::net::PeerId peer);
	// `wanted`, or with a number after it when another player has it.
	std::string UniqueName(std::string_view wanted, const Player* self) const;
	// A chat line to everyone, or a /command.
	void OnSay(Player& player, std::string_view text);
	void Command(Player& player, std::string_view line);
	// The server's notice: a strings-table key and its "{}"s, to one player
	// or to all.
	void Notice(ph::net::PeerId peer, const char* key, std::string_view name = {},
	            std::string_view extra = {});
	void NoticeAll(const char* key, std::string_view name = {}, std::string_view extra = {});
	void Broadcast(const sim::Chat& chat);
	// The match from its start: the field made again, no bots, no wave.
	void Restart();
	// After a step: wrecked bots go, wrecked players come back after a
	// while, and the next wave comes once the last one is gone.
	void Referee();
	void SendWave();
	// Bots together, from one side, `near` to `far` m from `center`, in
	// clear space, facing it; as many as there is room for. Returns how many.
	ph::u32 AddBots(ph::u32 count, ph::Vec2 center, ph::f32 near, ph::f32 far);
	void RemoveBots();
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
	MatchDesc started; // as Start had it: a restart goes back to it
	std::vector<Player> players;
	std::vector<Newcomer> newcomers;
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
