#include <sn/server/server.h>
#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

using namespace sn;
using namespace ph;

namespace
{
constexpr i64 START = 1'790'000'000;
constexpr const char* KEY = "0123456789abcdef0123456789abcdef";
constexpr const char* OTHER_KEY = "fedcba9876543210fedcba9876543210";
constexpr const char* THIRD_KEY = "00000000111111112222222233333333";

// Sign-ins answered at once: a proof is a player of the provider, or not.
class FakeChecker final : public server::SignInChecker
{
public:
	std::map<std::string, server::SignInAnswer> players; // by proof

	std::vector<sim::SignInOffer> Offers() const override
	{
		return {{"discord", "https://discord.example/authorize?client_id=1"}};
	}

	void Check(u64 ticket, const server::SignInProof& proof) override
	{
		const auto found = players.find(proof.proof);
		server::SignInAnswer answer;
		if (found == players.end())
			answer.error = "error.signin_failed";
		else
			answer = found->second;
		answer.provider = proof.provider;
		answers.emplace_back(ticket, answer);
	}

	bool Poll(u64& ticket, server::SignInAnswer& answer) override
	{
		if (answers.empty())
			return false;
		ticket = answers.front().first;
		answer = answers.front().second;
		answers.pop_front();
		return true;
	}

private:
	std::deque<std::pair<u64, server::SignInAnswer>> answers;
};

// Ann and Bo on Steam: "ann-ticket" and "bo-ticket" prove them.
FakeChecker& SteamPlayers()
{
	static FakeChecker checker;
	checker.players["ann-ticket"] = {"steam", "76561198000000001", "Ann", ""};
	checker.players["bo-ticket"] = {"steam", "76561198000000002", "Bo", ""};
	return checker;
}

// A client of the hub: connected, Hello for the hub said.
struct HubClient
{
	net::Client client;
	std::vector<sim::Chat> notices;
	std::vector<sim::Result> results;
	std::vector<sim::Signed> signs;
	sim::Account account;
	i64 now = 0;
	u32 profiles = 0;
	bool welcomed = false;
	sim::Welcome welcome;

	void Send(const std::vector<u8>& bytes)
	{
		net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Reliable);
	}

	void Request(const sim::Operation& operation)
	{
		Send(sim::Write(sim::Request{sim::ToJson(operation)}));
	}

	// Everything that came, sorted out.
	void Read()
	{
		net::Event event;
		while (net::Poll(client, event))
		{
			if (event.type != net::EventType::Message)
				continue;
			sim::Profile profile;
			sim::Chat chat;
			sim::Result result;
			sim::Signed sign;
			if (sim::Read(event.data, event.size, sign))
				signs.push_back(sign);
			else if (sim::Read(event.data, event.size, profile))
			{
				REQUIRE(sim::ReadProfile(profile.json, account, now));
				++profiles;
			}
			else if (sim::Read(event.data, event.size, chat))
				notices.push_back(chat);
			else if (sim::Read(event.data, event.size, result))
				results.push_back(result);
			else if (sim::Read(event.data, event.size, welcome))
				welcomed = true;
		}
	}

	bool Heard(const char* key) const
	{
		return std::any_of(notices.begin(), notices.end(),
		                   [key](const sim::Chat& chat) { return chat.text == key; });
	}
};

HubClient Connect(server::Server& host, const char* address, const char* key = KEY,
                  const char* name = "Ann", const char* proof = nullptr)
{
	HubClient hub;
	hub.client = net::Connect(address);
	net::Event event;
	net::Poll(hub.client, event);
	sim::Hello hello;
	hello.joining = sim::Joining::Hub;
	hub.Send(sim::Write(hello));
	host.Update(0.0f);
	sim::Login login{key, name};
	if (proof)
	{
		login.provider = "steam";
		login.proof = proof;
	}
	hub.Send(sim::Write(login));
	host.Update(0.0f);
	host.Update(0.0f); // the sign-in's answer
	hub.Read();
	return hub;
}

void Run(server::Server& host, HubClient& client, f32 seconds)
{
	for (u32 i = 0; i < u32(seconds * 30.0f + 0.5f); ++i)
	{
		host.Update(1.0f / 30.0f);
		client.Read();
	}
}

sim::Operation Launch(sim::Hex node, std::vector<u32> ships)
{
	sim::Operation operation;
	operation.kind = sim::Operation::Kind::Launch;
	operation.node = node;
	operation.ships = std::move(ships);
	return operation;
}
} // namespace

TEST_CASE("hub: a login gets the account; requests change it")
{
	server::MemoryStore store;
	i64 clock = START;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [&clock] { return clock; };
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-account", match, hub));
	HubClient ann = Connect(host, "loopback:hub-account");
	REQUIRE(ann.profiles == 1);
	CHECK(!ann.welcomed); // the hub: no ship yet
	CHECK(ann.now == START);
	CHECK(ann.account.name == "Ann");
	CHECK(ann.account.key.empty()); // never sent back
	CHECK(ann.account.ships.size() == 1);
	CHECK(host.GetPlayerCount() == 0);
	CHECK(host.GetClientCount() == 1);

	// An upgrade: paid, under way.
	sim::Operation upgrade;
	upgrade.kind = sim::Operation::Kind::Upgrade;
	upgrade.building = sim::Building::Mine;
	ann.Request(upgrade);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.profiles == 2);
	CHECK(ann.account.IsUpgrading(sim::Building::Mine));
	CHECK(ann.account.resources[sim::Resource::Metal] < 600.0);
	// The same building again is refused, with why; another one goes ahead.
	ann.Request(upgrade);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.profiles == 2);
	CHECK(ann.Heard("error.busy"));
	upgrade.building = sim::Building::Depot;
	ann.Request(upgrade);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.profiles == 3);
	CHECK(ann.account.IsUpgrading(sim::Building::Depot));

	// Time passes on the server's clock: it is done, and kept.
	clock += 3600;
	sim::Operation refresh;
	ann.Request(refresh);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.account.levels[u32(sim::Building::Mine)] == 2);
	// A guest: told so, and nothing of it saved.
	REQUIRE(ann.signs.size() == 1);
	CHECK(ann.signs[0].key == KEY);
	CHECK(ann.signs[0].provider.empty());
	CHECK(ann.signs[0].offers.empty()); // no checker, no sign-ins
	std::string saved;
	CHECK(!store.Load(server::AccountId(KEY), saved));
	CHECK(store.List().empty());

	// A flood is held back.
	for (u32 i = 0; i < 10; ++i)
		ann.Request(refresh);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.Heard("error.too_fast"));

	// The same key elsewhere takes the account over.
	HubClient again = Connect(host, "loopback:hub-account");
	CHECK(again.profiles == 1);
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.Heard("error.elsewhere"));
	// A key that is not one is refused.
	HubClient wrong = Connect(host, "loopback:hub-account", "not a key");
	CHECK(wrong.profiles == 0);
	CHECK(wrong.Heard("error.account"));
	net::Close(ann.client);
	net::Close(again.client);
	net::Close(wrong.client);
	host.Stop();
}

TEST_CASE("hub: a seed fills new accounts")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	hub.seed = [](sim::Account& account)
	{
		account.modules.push_back({"laser_s", 0.5f});
		account.resources[sim::Resource::Metal] = 12345.0;
	};
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-seed", match, hub));
	HubClient ann = Connect(host, "loopback:hub-seed");
	REQUIRE(ann.profiles == 1);
	CHECK(ann.account.resources[sim::Resource::Metal] == 12345.0);
	REQUIRE(!ann.account.modules.empty());
	CHECK(ann.account.modules.back().id == "laser_s");
	CHECK(ann.account.modules.back().condition == 0.5f);
	net::Close(ann.client);
	host.Stop();
}

TEST_CASE("hub: a battle is launched, cleared, looted, and its fleet comes home")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	const std::filesystem::path board =
		std::filesystem::temp_directory_path() / "sn_tests_leaderboard.json";
	std::error_code removed;
	std::filesystem::remove(board, removed);
	hub.leaderboard = board.string();
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	hub.signIn = &SteamPlayers();
	REQUIRE(host.Start("loopback:hub-battle", match, hub));
	HubClient ann = Connect(host, "loopback:hub-battle", KEY, "Ann", "ann-ticket");
	REQUIRE(ann.signs.size() == 1);
	CHECK(ann.signs[0].provider == "steam");
	const u32 ship = ann.account.ships[0].id;
	const f64 he3 = ann.account.resources[sim::Resource::He3];

	// Out of reach: refused.
	ann.Request(Launch({3, 0}, {ship}));
	host.Update(0.0f);
	ann.Read();
	CHECK(ann.Heard("error.node"));
	CHECK(host.GetBattleCount() == 0);

	ann.Request(Launch({1, 0}, {ship}));
	host.Update(0.0f);
	ann.Read();
	REQUIRE(ann.welcomed);
	CHECK(ann.welcome.kind == sim::MatchKind::Battle);
	CHECK(ann.welcome.ring == 1);
	CHECK(ann.welcome.modules[0] == "plasma_s");
	CHECK(host.GetBattleCount() == 1);
	CHECK(ann.account.ships[0].away);
	CHECK(ann.account.resources[sim::Resource::He3] < he3);
	CHECK(ann.account.command == doctest::Approx(9.0));

	// The node's enemies, made weak: our turret ends them as they come.
	sim::World* world = host.GetBattleWorld(0);
	REQUIRE(world);
	const sim::Node node = sim::NodeAt(sim::GetCatalog(), {1, 0});
	u32 enemies = 0;
	for (u32 slot = 0; slot < sim::MAX_SHIPS; ++slot)
	{
		sim::Ship& other = world->ships[slot];
		if (!world->shipIds[slot] || other.team != sim::BOTS)
			continue;
		++enemies;
		other.health = 1.0f;
		other.shield = 0.0f;
		other.hull.shieldRegen = 0.0f;
	}
	CHECK(enemies == node.enemies);
	for (u32 i = 0; i < 60 && !ann.Heard("battle.cleared"); ++i)
		Run(host, ann, 0.5f);
	REQUIRE(ann.Heard("battle.cleared"));

	// Crates where they broke apart: our ship flies over them.
	sim::Ship* own = sim::GetShip(*world, sim::ShipFromId(ann.welcome.ship));
	REQUIRE(own);
	u32 crates = 0;
	for (const sim::Crate& crate : world->crates)
	{
		if (crate.life <= 0.0f)
			continue;
		++crates;
		own->position = crate.position;
		own->velocity = {};
		Run(host, ann, 0.1f);
	}
	// One may have dropped near enough for our ship to take it already.
	CHECK(crates >= 1);
	CHECK(crates <= enemies);
	CHECK(own->cargoUsed > 3.0f - 0.5f); // charges spent, loot taken

	sim::Operation home;
	home.kind = sim::Operation::Kind::Return;
	ann.Request(home);
	Run(host, ann, 0.1f);
	REQUIRE(ann.results.size() == 1);
	sim::BattleReport report;
	REQUIRE(sim::FromJson(ann.results[0].json, report));
	CHECK(report.end == sim::BattleReport::End::Cleared);
	CHECK(report.kills == enemies);
	CHECK(report.lost == 0);
	CHECK(!report.loot.empty());
	CHECK(host.GetBattleCount() == 0);
	CHECK(!ann.account.ships[0].away);
	CHECK(sim::IsCleared(ann.account, {1, 0}));
	CHECK(ann.account.deepest == 1);
	CHECK(ann.account.resources[sim::Resource::Metal] > 600.0);
	REQUIRE(host.GetLeaderboard().Entries().size() == 1);
	CHECK(host.GetLeaderboard().Entries()[0].name == "Ann");
	CHECK(host.GetLeaderboard().Entries()[0].ring == 1);
	// The website's copy, without the account's id.
	std::string json;
	{
		FILE* file = std::fopen(board.string().c_str(), "rb");
		REQUIRE(file);
		char buffer[1024];
		json.assign(buffer, std::fread(buffer, 1, sizeof(buffer), file));
		std::fclose(file);
	}
	CHECK(json.find("\"name\":\"Ann\",\"ring\":1") != std::string::npos);
	CHECK(json.find("\"provider\":\"steam\"") != std::string::npos);
	CHECK(json.find(server::AccountId(KEY)) == std::string::npos);
	std::filesystem::remove(board, removed);
	net::Close(ann.client);
	host.Stop();
}

TEST_CASE("hub: a fleet left alone fights on, then comes home")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	hub.unattended = 2.0f;
	hub.signIn = &SteamPlayers();
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-alone", match, hub));
	HubClient ann = Connect(host, "loopback:hub-alone", KEY, "Ann", "ann-ticket");
	ann.Request(Launch({0, 1}, {ann.account.ships[0].id}));
	host.Update(0.0f);
	ann.Read();
	REQUIRE(ann.welcomed);
	net::Close(ann.client);
	host.Update(0.0f);
	CHECK(host.GetBattleCount() == 1);
	for (u32 i = 0; i < 90; ++i)
		host.Update(1.0f / 30.0f);
	CHECK(host.GetBattleCount() == 0);
	// Back, its ship home (or lost, if the enemies were quick).
	HubClient back = Connect(host, "loopback:hub-alone");
	REQUIRE(back.profiles == 1);
	REQUIRE(!back.account.ships.empty());
	CHECK(!back.account.ships[0].away);
	CHECK(back.account.stats.battles == 1);
	net::Close(back.client);

	// A guest's battle, left: gone with it.
	HubClient guest = Connect(host, "loopback:hub-alone", OTHER_KEY, "Cy");
	guest.Request(Launch({0, 1}, {guest.account.ships[0].id}));
	host.Update(0.0f);
	guest.Read();
	REQUIRE(guest.welcomed);
	net::Close(guest.client);
	for (u32 i = 0; i < 90; ++i)
		host.Update(1.0f / 30.0f);
	CHECK(host.GetBattleCount() == 0);
	HubClient again = Connect(host, "loopback:hub-alone", OTHER_KEY, "Cy");
	REQUIRE(again.profiles == 1);
	CHECK(again.account.stats.battles == 0); // a new guest
	net::Close(again.client);
	host.Stop();
}

TEST_CASE("hub: sign-ins save and rank; guests stay in memory")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	hub.signIn = &SteamPlayers();
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-signin", match, hub));

	// Bo plays as a guest, then signs in on the same connection: saved,
	// progress and all.
	HubClient bo = Connect(host, "loopback:hub-signin", OTHER_KEY, "Bo");
	REQUIRE(bo.signs.size() == 1);
	CHECK(bo.signs[0].provider.empty());
	REQUIRE(bo.signs[0].offers.size() == 1);
	CHECK(bo.signs[0].offers[0].provider == "discord");
	sim::Operation upgrade;
	upgrade.kind = sim::Operation::Kind::Upgrade;
	upgrade.building = sim::Building::Mine;
	bo.Request(upgrade);
	host.Update(0.0f);
	bo.Read();
	CHECK(store.List().empty());
	sim::Login login{OTHER_KEY, "Bo", "steam", "bo-ticket", ""};
	bo.Send(sim::Write(login));
	host.Update(0.0f);
	host.Update(0.0f);
	bo.Read();
	REQUIRE(bo.signs.size() == 2);
	CHECK(bo.signs[1].provider == "steam");
	CHECK(bo.signs[1].key == OTHER_KEY);
	CHECK(bo.account.IsUpgrading(sim::Building::Mine));
	REQUIRE(bo.account.identities.size() == 1);
	CHECK(bo.account.identities[0].id == "76561198000000002");
	std::string saved;
	REQUIRE(store.Load(server::AccountId(OTHER_KEY), saved));
	std::string linked;
	REQUIRE(store.LoadLink(server::LinkName("steam", "76561198000000002"), linked));
	CHECK(linked == server::AccountId(OTHER_KEY));

	// Ann signs in at once: her device's key makes the account.
	HubClient ann = Connect(host, "loopback:hub-signin", KEY, "Pilot", "ann-ticket");
	REQUIRE(ann.signs.size() == 1);
	CHECK(ann.signs[0].key == KEY);
	CHECK(ann.account.name == "Ann"); // Steam's name for her
	CHECK(store.Load(server::AccountId(KEY), saved));
	net::Close(ann.client);
	host.Update(0.0f);

	// On another device: her account wins, with a key for that device.
	HubClient elsewhere = Connect(host, "loopback:hub-signin", THIRD_KEY, "Ann", "ann-ticket");
	REQUIRE(elsewhere.signs.size() == 1);
	const std::string handed = elsewhere.signs[0].key;
	CHECK(handed != THIRD_KEY);
	CHECK(server::IsKey(handed));
	CHECK(elsewhere.account.name == "Ann");
	CHECK(!store.Load(server::AccountId(THIRD_KEY), saved)); // no account of its own
	net::Close(elsewhere.client);
	host.Update(0.0f);
	// The handed key opens it without a sign-in.
	HubClient keyed = Connect(host, "loopback:hub-signin", handed.c_str(), "Ann");
	REQUIRE(keyed.signs.size() == 1);
	CHECK(keyed.signs[0].provider == "steam");
	CHECK(keyed.account.identities.size() == 1);
	net::Close(keyed.client);
	host.Update(0.0f);

	// A proof the provider does not take: a guest, told why.
	HubClient wrong = Connect(host, "loopback:hub-signin", THIRD_KEY, "Di", "forged");
	REQUIRE(wrong.signs.size() == 1);
	CHECK(wrong.signs[0].error == "error.signin_failed");
	CHECK(wrong.signs[0].provider.empty());
	CHECK(wrong.profiles == 1);
	net::Close(wrong.client);
	net::Close(bo.client);
	host.Update(0.0f);
	CHECK(store.List().size() == 2);
	host.Stop();
}

// The admin page's side: JSON in, JSON out.
struct AdminPage
{
	net::Client client;

	std::string Ask(server::Server& host, const std::string& json)
	{
		net::Send(client, json.data(), u32(json.size()), net::Delivery::Reliable);
		host.Update(0.0f);
		std::string answer;
		net::Event event;
		while (net::Poll(client, event))
		{
			if (event.type == net::EventType::Message)
				answer.assign(reinterpret_cast<const char*>(event.data), event.size);
		}
		return answer;
	}
};

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

TEST_CASE("hub: the admin page")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	hub.signIn = &SteamPlayers();
	hub.admin = "loopback:hub-admin-page";
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-admin", match, hub));
	HubClient ann = Connect(host, "loopback:hub-admin", KEY, "Ann", "ann-ticket");
	HubClient guest = Connect(host, "loopback:hub-admin", OTHER_KEY, "Cy");
	AdminPage page;
	page.client = net::Connect("loopback:hub-admin-page");
	host.Update(0.0f);
	const std::string ann_id = server::AccountId(KEY);

	std::string answer = page.Ask(host, R"({"op":"status","tag":7})");
	CHECK(Has(answer, R"("tag":7)"));
	CHECK(Has(answer, R"("op":"status")"));
	CHECK(Has(answer, R"("accounts":1)"));
	CHECK(Has(answer, R"("where":"hub")"));
	answer = page.Ask(host, R"({"op":"accounts"})");
	CHECK(Has(answer, R"("total":2)"));
	CHECK(Has(answer, R"("saved":false)")); // the guest
	answer = page.Ask(host, R"({"op":"accounts","filter":"signed","query":"7656119800"})");
	CHECK(Has(answer, R"("total":1)"));
	CHECK(Has(answer, ann_id.c_str()));

	answer = page.Ask(host, R"({"op":"grant","account":")" + ann_id +
	                            R"(","resources":{"metal":1000,"chips":-1000}})");
	CHECK(Has(answer, R"("op":"account")"));
	CHECK(Has(answer, R"("chips":0)"));
	ann.Read();
	CHECK(ann.account.resources[sim::Resource::Metal] > 1500.0); // told at once
	answer = page.Ask(host, R"({"op":"rename","account":")" + ann_id + R"(","name":"Annie"})");
	CHECK(Has(answer, R"("name":"Annie")"));
	answer = page.Ask(host, R"({"op":"hide","account":")" + ann_id + R"(","hidden":true})");
	CHECK(Has(answer, R"("hidden":true)"));
	answer = page.Ask(host, R"({"op":"unlink","account":")" + ann_id + R"(","provider":"steam"})");
	CHECK(Has(answer, R"("op":"error")")); // her last sign-in
	answer = page.Ask(host, R"({"op":"delete","account":")" + ann_id + R"("})");
	CHECK(Has(answer, R"("error":"online")"));
	answer = page.Ask(host, R"({"op":"account","account":"../../etc"})");
	CHECK(Has(answer, R"("error":"unknown account")"));
	answer = page.Ask(host, R"({"op":"nothing"})");
	CHECK(Has(answer, R"("error":"unknown op")"));

	// A ban sends her away, and keeps her out.
	answer = page.Ask(host, R"({"op":"ban","account":")" + ann_id + R"(","banned":true})");
	CHECK(Has(answer, R"("banned":true)"));
	ann.Read();
	CHECK(ann.Heard("error.banned"));
	net::Close(ann.client);
	host.Update(0.0f);
	HubClient back = Connect(host, "loopback:hub-admin", KEY, "Ann", "ann-ticket");
	CHECK(back.Heard("error.banned"));
	CHECK(back.profiles == 0);
	net::Close(back.client);
	host.Update(0.0f);

	// Deleted: gone from the store, her sign-in free.
	answer = page.Ask(host, R"({"op":"delete","account":")" + ann_id + R"("})");
	CHECK(Has(answer, R"("op":"deleted")"));
	std::string json;
	CHECK(!store.Load(ann_id, json));
	CHECK(!store.LoadLink(server::LinkName("steam", "76561198000000001"), json));
	answer = page.Ask(host, R"({"op":"status"})");
	CHECK(Has(answer, R"("accounts":0)"));
	net::Close(page.client);
	net::Close(guest.client);
	host.Stop();
}

TEST_CASE("hub: the admin keeps a guest; its key opens it until a sign-in joins it")
{
	server::MemoryStore store;
	server::HubDesc hub;
	hub.store = &store;
	hub.clock = [] { return START; };
	hub.signIn = &SteamPlayers();
	hub.admin = "loopback:hub-keep-page";
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-keep", match, hub));
	HubClient bo = Connect(host, "loopback:hub-keep", OTHER_KEY, "Bo");
	AdminPage page;
	page.client = net::Connect("loopback:hub-keep-page");
	host.Update(0.0f);
	const std::string id = server::AccountId(OTHER_KEY);
	std::string json;
	CHECK(!store.Load(id, json)); // a guest: in memory only

	std::string answer = page.Ask(host, R"({"op":"keep","account":")" + id + R"("})");
	CHECK(Has(answer, R"("kept":true)"));
	CHECK(Has(answer, R"("saved":true)"));
	REQUIRE(store.Load(id, json));
	CHECK(Has(json, R"("kept":true)"));
	answer = page.Ask(host, R"({"op":"status"})");
	CHECK(Has(answer, R"("accounts":1)"));
	answer =
		page.Ask(host, R"({"op":"grant","account":")" + id + R"(","resources":{"metal":1000}})");
	CHECK(Has(answer, R"("op":"account")"));

	// Gone and back: its key opens it, and the hub hears it is saved.
	net::Close(bo.client);
	host.Update(0.0f);
	HubClient back = Connect(host, "loopback:hub-keep", OTHER_KEY, "Bo");
	REQUIRE(!back.signs.empty());
	CHECK(back.signs.back().provider == "device");
	CHECK(back.account.resources[sim::Resource::Metal] > 1500.0);

	// A sign-in joins it: its own from now on, kept no more, counted once.
	sim::Login login{OTHER_KEY, "Bo", "steam", "bo-ticket", ""};
	back.Send(sim::Write(login));
	host.Update(0.0f);
	host.Update(0.0f);
	back.Read();
	CHECK(back.signs.back().provider == "steam");
	CHECK(back.account.resources[sim::Resource::Metal] > 1500.0);
	REQUIRE(store.Load(id, json));
	CHECK(!Has(json, R"("kept")"));
	answer = page.Ask(host, R"({"op":"status"})");
	CHECK(Has(answer, R"("accounts":1)"));
	answer = page.Ask(host, R"({"op":"keep","account":")" + id + R"("})");
	CHECK(Has(answer, R"("op":"error")")); // saved by its sign-in already
	net::Close(page.client);
	net::Close(back.client);
	host.Stop();
}

TEST_CASE("hub: closed without a store; skirmish clients cannot log in")
{
	server::Server host;
	server::MatchDesc match;
	match.waves = false;
	REQUIRE(host.Start("loopback:hub-closed", match));
	const net::Client client = net::Connect("loopback:hub-closed");
	net::Event event;
	net::Poll(client, event);
	sim::Hello hello;
	hello.joining = sim::Joining::Hub;
	const std::vector<u8> bytes = sim::Write(hello);
	net::Send(client, bytes.data(), u32(bytes.size()), net::Delivery::Reliable);
	host.Update(0.0f);
	sim::Refusal refusal;
	bool refused = false;
	while (net::Poll(client, event))
		refused |=
			event.type == net::EventType::Message && sim::Read(event.data, event.size, refusal);
	CHECK(refused);
	CHECK(refusal.reason == sim::RefusalReason::Full);
	net::Close(client);
	host.Stop();
}

TEST_CASE("hub: accounts in files, and the leaderboard's order")
{
	namespace fs = std::filesystem;
	const fs::path folder = fs::temp_directory_path() / "sn_tests_accounts";
	std::error_code error;
	fs::remove_all(folder, error);
	{
		server::FileStore store(folder.string());
		CHECK(store.List().empty());
		REQUIRE(store.Save("abc", "{\"x\": 1}"));
		REQUIRE(store.Save("abc", "{\"x\": 2}"));
		REQUIRE(store.Save("def", "{}"));
		std::string json;
		REQUIRE(store.Load("abc", json));
		CHECK(json == "{\"x\": 2}");
		CHECK(!store.Load("nothing", json));
		std::vector<std::string> ids = store.List();
		std::sort(ids.begin(), ids.end());
		CHECK(ids == std::vector<std::string>{"abc", "def"});
	}
	fs::remove_all(folder, error);

	CHECK(server::IsKey(KEY));
	CHECK(!server::IsKey("0123"));
	CHECK(!server::IsKey("0123456789ABCDEF0123456789ABCDEF"));
	CHECK(server::AccountId(KEY).size() == 16);
	CHECK(server::AccountId(KEY) != server::AccountId("1123456789abcdef0123456789abcdef"));

	server::Leaderboard board;
	CHECK(board.Update("a", "Ann", 2, 100));
	CHECK(board.Update("b", "Bo", 3, 200));
	CHECK(board.Update("c", "Cy", 2, 50));
	CHECK(!board.Update("c", "Cy", 2, 50));
	CHECK(!board.Update("d", "Di", 0, 10)); // nothing cleared: not on it
	REQUIRE(board.Entries().size() == 3);
	CHECK(board.Entries()[0].name == "Bo");
	CHECK(board.Entries()[1].name == "Cy"); // ring 2 first
	CHECK(board.Entries()[2].name == "Ann");
	const std::string json = board.Json(1, 300, 2);
	CHECK(json.find("\"name\":\"Bo\"") != std::string::npos);
	CHECK(json.find("Ann") == std::string::npos); // the first two only
	CHECK(json.find("\"rank\":2") != std::string::npos);
}
