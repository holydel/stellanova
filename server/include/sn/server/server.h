#pragma once

#include <sn/bots/pilot.h>
#include <sn/server/accounts.h>
#include <sn/server/signin.h>
#include <sn/sim/account.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// A game's authority (docs/adr/0007-local-server.md, 0009-bots-v0.md,
// 0013-ships-modules-damage.md, 0014-accounts-on-the-game-server.md,
// 0016-sign-in-and-admin.md). It
// listens at an address and hosts matches:
//
// - the skirmish, which a client that says Hello for it joins at once: waves
//   of enemy bots, each bigger, players brought back when destroyed, chat and
//   commands (/help lists them);
// - battles: a client that comes for the hub logs in to its account, changes
//   it by requests, and launches its fleet at a node of the star map, alone
//   against the node's enemies. The battle ends cleared, returned or lost,
//   and the account gets what came home. A signed-in account is saved and
//   ranked; a guest's lives only while its client is connected.
//
// An admin listener, when given, takes the admin page's JSON requests.
//
// Each match runs the sim at its fixed tick, flies its bots, and sends its
// players the tick's events and a snapshot after each tick. No globals: a
// client can host one in its own process.
namespace sn::server
{
using ph::f32;

// The skirmish's rules.
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

// The hub: where accounts are kept. Without a store, it is closed.
struct HubDesc
{
	AccountStore* store = nullptr;
	// Checks sign-ins; none: every client is a guest.
	SignInChecker* signIn = nullptr;
	// Guests are saved too (a local practice campaign); otherwise they live
	// only while connected.
	bool keepGuests = false;
	// The admin page's listener ("ws:127.0.0.1:27081"; "": none).
	std::string admin;
	std::string leaderboard;        // a file the leaderboard goes to ("": none)
	std::function<ph::i64()> clock; // seconds since 1970; empty: the system's
	// Fills each new account after the catalog's start (the client's
	// inventory test: many things to drag); empty: the start alone.
	std::function<void(sim::Account&)> seed;
	ph::f32 longestBattle = 15.0f * 60.0f; // s: then it ends as returned
	ph::f32 unattended = 120.0f;           // s a battle goes on without its player
};

class Server
{
public:
	// "loopback:<name>" for a local game. False when the address is taken.
	bool Start(const char* address, const MatchDesc& match = {}, const HubDesc& hub = {});
	void Stop();
	bool IsRunning() const { return bool(listener); }
	// What the clients said, then the ticks that `dt` seconds cover.
	void Update(ph::f32 dt);
	// The skirmish's world, for tests and tools (to change between updates).
	const sim::World* GetWorld() const;
	sim::World* GetWorld();
	// The skirmish's wave: 0 before the first; its players and bots.
	ph::u32 GetWave() const;
	ph::u32 GetPlayerCount() const;
	ph::u32 GetBotCount() const;
	// Everyone connected who said Hello, in the hub or a match.
	ph::u32 GetClientCount() const { return ph::u32(clients.size()); }
	ph::net::PeerId GetClientPeer(ph::u32 index) const { return clients[index].peer; }
	ph::u32 GetBattleCount() const { return ph::u32(matches.size() - 1); }
	// A battle's world, for tests: by its index (0 to GetBattleCount()).
	sim::World* GetBattleWorld(ph::u32 index);
	ph::net::Server GetListener() const { return listener; }
	const Leaderboard& GetLeaderboard() const { return leaderboard; }
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

	struct Match;

	// A connection that said Hello.
	struct Client
	{
		ph::net::PeerId peer = 0;
		std::string name;         // its Name, else "Pilot <n>", unique on the server
		bool announced = false;   // "joined" went out (the skirmish)
		ph::f32 chatLines = 5.0f; // lines it may say now: one more a second, five at most
		ph::f32 requests = 5.0f;  // requests it may make now: two more a second
		std::string account;      // its account's id, once logged in
		Match* match = nullptr;   // where its ship is; none in the hub
		// A Login whose sign-in is being checked: its ticket with the checker.
		ph::u64 signIn = 0;
		sim::Login login;
	};

	// A saved account, for the admin's lists without reading every file.
	struct Summary
	{
		std::string name;
		std::vector<sim::Identity> identities;
		ph::u32 deepest = 0;
		ph::u32 battles = 0;
		ph::u32 kills = 0;
		ph::i64 created = 0;
		ph::i64 updated = 0;
		bool banned = false;
		bool hidden = false;
	};

	// A client's ship in a match.
	struct Player
	{
		ph::net::PeerId peer = 0;
		sim::ShipHandle ship;
		ph::Vec2 home;       // where its ship starts, and comes back (the skirmish)
		ph::f32 down = 0.0f; // s its ship has been a wreck
		bots::Pilot pilot;   // with MatchDesc::autopilot
		// Its controls not yet applied, oldest first: one set each tick.
		std::vector<Controls> queued;
		ph::u32 received = 0; // the newest number that came
		ph::u32 applied = 0;  // the last number applied: its snapshots say so
		ph::u32 idle = 0;     // ticks since the last set came
	};

	// A bot of the enemies.
	struct Enemy
	{
		sim::ShipHandle ship;
		bots::Pilot pilot;
	};

	// One of a battle's fleet, as launched: the first is its player's, the
	// others escorts that pilots fly.
	struct FleetShip
	{
		ph::u32 record = 0; // the account's ShipRecord id
		sim::ShipFit fit;
		sim::ShipHandle ship;
		bots::Pilot pilot;
		std::vector<sim::Stack> haul; // what it picked up
	};

	struct Match
	{
		sim::MatchKind kind = sim::MatchKind::Skirmish;
		MatchDesc rules;
		std::unique_ptr<sim::World> world;
		std::vector<Player> players;
		std::vector<Enemy> enemies;
		bots::PilotSkill skill; // the enemies'
		ph::u32 wave = 0;
		ph::f32 untilWave = 0.0f; // s, counted while no wave is out
		ph::u32 seed = 1;
		ph::f32 sinceTick = 0.0f;
		// A battle's.
		std::string account;
		sim::Node node;
		std::vector<FleetShip> fleet;
		std::vector<sim::Loot> loot; // each crate's, by its index in the world
		ph::u32 kills = 0;
		bool cleared = false;
		ph::f32 age = 0.0f;          // s
		ph::f32 sinceCleared = 0.0f; // s
		ph::f32 sinceLost = 0.0f;    // s since the last of the fleet broke apart
		ph::f32 unattended = 0.0f;   // s without its player
		bool returning = false;      // its player asked to go home
	};

	Client* FindClient(ph::net::PeerId peer);
	void OnMessage(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void OnHello(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void OnName(Client& client, std::string_view name);
	void OnInput(Client& client, const sim::Input& input);
	// Why not, then a moment later, the door.
	void Refuse(ph::net::PeerId peer, sim::RefusalReason reason);
	// Newcomers that never said Hello, and refused ones, are disconnected.
	void Admit(ph::f32 dt);
	void Leave(ph::net::PeerId peer);
	// `wanted`, or with a number after it when another client has it.
	std::string UniqueName(std::string_view wanted, const Client* self) const;
	// A chat line to the speaker's match, or a /command.
	void OnSay(Client& client, std::string_view text);
	void Command(Client& client, std::string_view line);
	// The server's notice: a strings-table key and its "{}"s, to one client,
	// or to every player of a match.
	void Notice(ph::net::PeerId peer, const char* key, std::string_view name = {},
	            std::string_view extra = {});
	void NoticeAll(const Match& match, const char* key, std::string_view name = {},
	               std::string_view extra = {});
	void Broadcast(const Match& match, const sim::Chat& chat);

	// The hub (server.cpp).
	ph::i64 Now() const;
	void OnLogin(Client& client, const sim::Login& login);
	// The checker's answers to the Logins waiting for them.
	void PollSignIns();
	// The Login, once its sign-in was checked (`answer`), or without one.
	void FinishLogin(Client& client, const sim::Login& login, const SignInAnswer* answer);
	// The account a device's key opens: saved, handed out at sign-in, or a
	// guest in memory; "" for none. False when the key's account is someone
	// else's (a hash that collides).
	bool FindByKey(const std::string& key, std::string& id);
	// A new key for a signed-in account, its link kept; the oldest goes
	// past MAX_KEYS_KEPT.
	std::string HandOutKey(const std::string& id, sim::Account& account);
	// Saved: signed in (or any, with keepGuests). Ranked: saved, signed
	// in, not hidden, not banned.
	bool Saves(const sim::Account& account) const;
	bool Ranks(const sim::Account& account) const;
	void OnRequest(Client& client, std::string_view json);
	// The account by id, read from the store on first use; null for none.
	sim::Account* LoadAccount(const std::string& id);
	void SaveAccount(const std::string& id);
	// Its account to a client, as it is now.
	void SendProfile(const Client& client);
	static Summary SummaryOf(const sim::Account& account);
	// Accounts no client and no battle uses leave memory.
	void ForgetAccount(const std::string& id);
	bool InUse(const std::string& id) const;
	void UpdateLeaderboard(const std::string& id, const sim::Account& account);
	// To its file, when it changed.
	void WriteLeaderboard();

	// The admin page (admin.cpp): JSON in, JSON out.
	void UpdateAdmin();
	void OnAdmin(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	// The answer's JSON, or "" with `why` when it failed.
	std::string AdminStatus();
	std::string AdminAccounts(std::string_view query, std::string_view filter, ph::u32 offset,
	                          ph::u32 limit);
	std::string AdminAccount(const std::string& id);
	std::string AdminDelete(const std::string& id, std::string& why);
	// After an admin's change: saved, ranked, and its client told.
	void AdminChanged(const std::string& id, sim::Account& account);

	// Matches (match.cpp).
	std::unique_ptr<Match> NewSkirmish(const MatchDesc& desc) const;
	// A client's ship into the skirmish, with its Welcome; false when full.
	bool JoinSkirmish(Client& client);
	// A battle for the client's account, its Welcome sent; false (with why)
	// when the launch is refused.
	bool LaunchBattle(Client& client, sim::Hex node, const std::vector<ph::u32>& ships,
	                  std::string& why);
	// `modules`: the ship's slots' catalog ids, for its HUD.
	void SendWelcome(const Match& match, const Player& player, const sim::Ship& ship,
	                 const std::vector<std::string>& modules);
	void RemovePlayer(Match& match, ph::net::PeerId peer);
	// Its ticks for `dt`.
	void Run(Match& match, ph::f32 dt);
	// Each player's next controls, for this tick.
	void ApplyControls(Match& match);
	void FlyBots(Match& match);
	// After a step: the skirmish's wrecks, comebacks and waves; a battle's
	// crates, haul and end.
	void RefereeSkirmish(Match& match);
	void RefereeBattle(Match& match);
	bool BattleOver(const Match& match) const;
	// The account gets what came home; its client the Result and a Profile.
	void EndBattle(Match& match);
	// The match from its start: the field made again, no bots, no wave.
	void Restart(Match& match);
	void SendWave(Match& match);
	// Bots of the enemies' fits together, from one side, `near` to `far` m
	// from `center`, in clear space, facing it; as many as there is room for
	// in snapshots. Returns how many.
	ph::u32 AddBots(Match& match, ph::u32 count, ph::Vec2 center, ph::f32 near, ph::f32 far,
	                ph::f32 strength);
	void RemoveBots(Match& match);
	// A point near `near` where a circle of `radius` touches no rock.
	ph::Vec2 FindRoom(const sim::World& world, ph::Vec2 near, ph::f32 radius) const;
	ph::f32 Random(Match& match);
	// After each tick: what happened (reliable), then where things are.
	void SendEvents(Match& match);
	void SendSnapshots(Match& match);
	// Sends and counts.
	void Send(ph::net::PeerId peer, const std::vector<ph::u8>& bytes, ph::net::Delivery delivery);

	ph::net::Server listener;
	ph::net::Server adminListener;
	ph::i64 startedAt = 0;
	MatchDesc started; // as Start had it: a restart goes back to it
	HubDesc hub;
	// A peer that connected and has not said Hello: it must, soon. A refused
	// one waits a moment for its Refusal to go out.
	struct Newcomer
	{
		ph::net::PeerId peer = 0;
		ph::f32 age = 0.0f; // s
		bool refused = false;
	};
	std::vector<Newcomer> newcomers;
	std::vector<Client> clients;
	// The skirmish first, then the battles.
	std::vector<std::unique_ptr<Match>> matches;
	std::unordered_map<std::string, sim::Account> accounts; // in use, by id
	ph::u32 accountCount = 0;                               // in the store
	std::unordered_map<std::string, Summary> summaries;     // the store's, by id
	ph::u64 nextSignIn = 0;
	Leaderboard leaderboard;
	bool leaderboardChanged = false;
	sim::Snapshot snapshot; // reused each tick
	sim::Events events;
	sim::MessageCounts sent;
	sim::MessageCounts received;
};
} // namespace sn::server
