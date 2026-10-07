#include <sn/sim/account.h>

#include <doctest/doctest.h>

#include <algorithm>

using namespace sn::sim;

namespace
{
constexpr i64 START = 1'790'000'000; // seconds since 1970, some day in 2026

Account Fresh() { return NewAccount(GetCatalog(), "key", "Pilot", START); }

u32 Count(const std::vector<Stack>& stacks, const char* id)
{
	for (const Stack& stack : stacks)
	{
		if (stack.id == id)
			return stack.count;
	}
	return 0;
}
} // namespace

TEST_CASE("account: a new colony as the catalog starts it")
{
	const Account account = Fresh();
	CHECK(account.resources[Resource::Metal] == 600.0);
	CHECK(account.resources[Resource::He3] == 200.0);
	CHECK(account.command == 10.0);
	REQUIRE(account.ships.size() == 1);
	const ShipRecord& ship = account.ships[0];
	CHECK(ship.fit.hull == "lancer");
	REQUIRE(ship.fit.slots.size() == 2);
	CHECK(ship.fit.slots[0].id == "plasma_s");
	CHECK(ship.fit.slots[1].id == "shield_booster_s");
	CHECK(Count(ship.fit.hold, "plasma_charge_s") == 300);
	CHECK(account.modules.size() == 2);
	CHECK(Count(account.items, "plasma_charge_s") == 300);
	CHECK(std::count(account.blueprints.begin(), account.blueprints.end(), "lancer") == 1);
}

TEST_CASE("account: the colony produces from the clock, up to the depot's storage")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	Advance(catalog, account, START + 3600);
	CHECK(account.resources[Resource::Metal] == doctest::Approx(600.0 + 120.0));
	CHECK(account.resources[Resource::He3] == doctest::Approx(200.0 + 60.0));
	CHECK(account.resources[Resource::Chips] == doctest::Approx(100.0 + 30.0));
	// A week away: full storage, never more.
	Advance(catalog, account, START + 7 * 24 * 3600);
	CHECK(account.resources[Resource::Metal] == doctest::Approx(StorageOf(catalog, 1)));
	// Loot may fill it past the cap; production then waits.
	account.resources[Resource::Metal] = 5000.0;
	Advance(catalog, account, START + 8 * 24 * 3600);
	CHECK(account.resources[Resource::Metal] == 5000.0);
	// Command points come back, up to the cap.
	account.command = 0.0;
	Advance(catalog, account, START + 8 * 24 * 3600 + 720 * 3);
	CHECK(account.command == doctest::Approx(3.0));
}

TEST_CASE("account: an upgrade costs and takes time; buildings upgrade at once")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	std::string why;
	const Resources cost = UpgradeCost(catalog, Building::Mine, 1);
	REQUIRE(Upgrade(catalog, account, Building::Mine, START, why));
	CHECK(account.resources[Resource::Metal] == doctest::Approx(600.0 - cost[Resource::Metal]));
	// The same building twice: no. Another one meanwhile: yes.
	CHECK(!Upgrade(catalog, account, Building::Mine, START + 1, why));
	CHECK(why == "error.busy");
	REQUIRE(Upgrade(catalog, account, Building::Fab, START + 10, why));
	CHECK(account.IsUpgrading(Building::Mine));
	CHECK(account.IsUpgrading(Building::Fab));
	const i64 done = START + i64(UpgradeSeconds(catalog, 1));
	Advance(catalog, account, done - 1);
	CHECK(account.levels[u32(Building::Mine)] == 1);
	Advance(catalog, account, done);
	CHECK(account.levels[u32(Building::Mine)] == 2);
	CHECK(!account.IsUpgrading(Building::Mine));
	CHECK(account.levels[u32(Building::Fab)] == 1); // ten seconds later
	Advance(catalog, account, done + 10);
	CHECK(account.levels[u32(Building::Fab)] == 2);
	CHECK(!account.IsUpgrading(Building::Fab));
	// The next level costs more.
	CHECK(UpgradeCost(catalog, Building::Mine, 2)[Resource::Metal] > cost[Resource::Metal]);
	CHECK(ProductionPerHour(catalog, Building::Mine, 2) == doctest::Approx(180.0));
	// Without the metal, why not.
	account.resources[Resource::Metal] = 0.0;
	CHECK(!Upgrade(catalog, account, Building::Mine, done, why));
	CHECK(why == "error.metal");
}

TEST_CASE("account: building from blueprints")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	account.resources[Resource::Metal] = 2000.0;
	std::string why;
	REQUIRE(Build(catalog, account, "lancer", START, why));
	CHECK(account.ships.size() == 2);
	for (const FittedModule& slot : account.ships[1].fit.slots)
		CHECK(slot.id.empty()); // an empty hull
	REQUIRE(Build(catalog, account, "laser_s", START, why));
	CHECK(account.modules.size() == 3);
	REQUIRE(Build(catalog, account, "plasma_charge_s", START, why));
	CHECK(Count(account.items, "plasma_charge_s") ==
	      300 + catalog.FindAmmo("plasma_charge_s")->batch);
	CHECK(!Build(catalog, account, "raider", START, why));
	CHECK(why == "error.blueprint");
}

TEST_CASE("account: fitting, loading and repairing a ship")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	const u32 id = account.ships[0].id;
	std::string why;
	// The laser in storage (index 0) for the plasma gun: they swap.
	REQUIRE(account.modules[0].id == "laser_s");
	REQUIRE(FitModule(catalog, account, id, 0, 0, why));
	CHECK(account.ships[0].fit.slots[0].id == "laser_s");
	CHECK(account.modules.back().id == "plasma_s");
	// A battery cannot go in the external slot.
	REQUIRE(account.modules[0].id == "capacitor_battery_s");
	CHECK(!FitModule(catalog, account, id, 0, 0, why));
	CHECK(why == "error.slot");
	// Empty the internal slot.
	REQUIRE(FitModule(catalog, account, id, 1, -1, why));
	CHECK(account.ships[0].fit.slots[1].id.empty());

	// The hold: 20 m^3, 300 charges aboard (3 m^3).
	REQUIRE(LoadHold(catalog, account, id, "plasma_charge_s", 300, why));
	CHECK(Count(account.ships[0].fit.hold, "plasma_charge_s") == 600);
	CHECK(Count(account.items, "plasma_charge_s") == 0);
	CHECK(!LoadHold(catalog, account, id, "plasma_charge_s", 1, why));
	CHECK(why == "error.storage");
	account.items.push_back({"plasma_charge_s", 5000});
	CHECK(!LoadHold(catalog, account, id, "plasma_charge_s", 2000, why));
	CHECK(why == "error.hold");
	REQUIRE(LoadHold(catalog, account, id, "plasma_charge_s", -100, why));
	CHECK(Count(account.ships[0].fit.hold, "plasma_charge_s") == 500);

	account.ships[0].fit.condition = 0.5f;
	const f64 cost = RepairCost(catalog, account.ships[0]);
	CHECK(cost == doctest::Approx(400.0 * 0.5 * catalog.rules.repairMetal));
	const f64 metal = account.resources[Resource::Metal];
	REQUIRE(Repair(catalog, account, id, START, why));
	CHECK(account.ships[0].fit.condition == 1.0f);
	CHECK(account.resources[Resource::Metal] == doctest::Approx(metal - cost));
}

TEST_CASE("starmap: rings of hexes, nodes from the seed, harder outward")
{
	const Catalog& catalog = GetCatalog();
	for (u32 ring = 0; ring < 5; ++ring)
	{
		const std::vector<Hex> hexes = RingHexes(ring);
		CHECK(hexes.size() == (ring == 0 ? 1 : 6 * ring));
		for (const Hex hex : hexes)
			CHECK(RingOf(hex) == ring);
	}
	CHECK(Distance({2, -1}, {2, -1}) == 0);
	CHECK(Distance({0, 0}, Neighbor({0, 0}, 3)) == 1);
	const Node node = NodeAt(catalog, {3, -1});
	const Node again = NodeAt(catalog, {3, -1});
	CHECK(node.ring == 3);
	CHECK(node.seed == again.seed);
	CHECK(node.kind == again.kind);
	CHECK(NodeAt(catalog, {1, 0}).strength == doctest::Approx(1.0f));
	CHECK(NodeAt(catalog, {6, 0}).strength > NodeAt(catalog, {2, 0}).strength);
	u32 near = 0;
	u32 far = 0;
	for (const Hex hex : RingHexes(1))
		near += NodeAt(catalog, hex).enemies;
	for (const Hex hex : RingHexes(8))
		far += NodeAt(catalog, hex).enemies;
	CHECK(far / 48 > near / 6);
}

TEST_CASE("starmap: what an account may attack and see")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	CHECK(CanAttack(account, {1, 0}));
	CHECK(!CanAttack(account, {2, 0}));
	CHECK(!CanAttack(account, {0, 0}));
	CHECK(IsVisible(catalog, account, {2, 0}));
	CHECK(!IsVisible(catalog, account, {3, 0}));
	account.cleared.push_back({1, 0});
	CHECK(CanAttack(account, {2, 0}));
	CHECK(CanAttack(account, {2, -1}));
	CHECK(!CanAttack(account, {-2, 0}));
	CHECK(IsVisible(catalog, account, {3, 0}));
	CHECK(!IsVisible(catalog, account, {-3, 0}));
}

TEST_CASE("account: a launch pays, and the outcome brings home loot and losses")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	account.resources[Resource::Metal] = 2000.0;
	std::string why;
	REQUIRE(Build(catalog, account, "lancer", START, why));
	const u32 first = account.ships[0].id;
	const u32 second = account.ships[1].id;
	// An empty hull can still launch; a node out of reach cannot.
	CHECK(!Launch(catalog, account, {3, 0}, {first}, START, why));
	CHECK(why == "error.node");
	const f64 he3 = account.resources[Resource::He3];
	REQUIRE(Launch(catalog, account, {1, 0}, {first, second}, START, why));
	CHECK(account.command == doctest::Approx(9.0));
	CHECK(account.resources[Resource::He3] ==
	      doctest::Approx(he3 - CostOfLaunch(catalog, {1, 0}, 2).fuel));
	CHECK(account.ships[0].away);
	CHECK(!Launch(catalog, account, {1, 0}, {first}, START, why));
	CHECK(!FitModule(catalog, account, first, 0, 0, why));

	BattleOutcome outcome;
	outcome.node = {1, 0};
	outcome.cleared = true;
	outcome.kills = 2;
	ShipOutcome home;
	home.id = first;
	home.condition = 0.6f;
	home.modules = {0.5f, 1.0f};
	home.hold = {{"metal", 150}, {"chips", 20}, {"plasma_charge_s", 250}, {"laser_s", 1}};
	outcome.ships.push_back(home);
	ShipOutcome lost;
	lost.id = second;
	lost.lost = true;
	outcome.ships.push_back(lost);
	const f64 metal = account.resources[Resource::Metal];
	ApplyOutcome(catalog, account, outcome, START + 10);
	REQUIRE(account.ships.size() == 1);
	const ShipRecord& ship = account.ships[0];
	CHECK(!ship.away);
	CHECK(ship.fit.condition == 0.6f);
	CHECK(ship.fit.slots[0].condition == 0.5f);
	CHECK(Count(ship.fit.hold, "plasma_charge_s") == 250);
	CHECK(Count(ship.fit.hold, "metal") == 0);
	CHECK(account.resources[Resource::Metal] == doctest::Approx(metal + 150.0).epsilon(0.001));
	CHECK(account.modules.back().id == "laser_s");
	CHECK(IsCleared(account, {1, 0}));
	CHECK(account.deepest == 1);
	CHECK(account.deepestAt == START + 10);
	CHECK(account.stats.lost == 1);
	CHECK(account.stats.kills == 2);
}

TEST_CASE("account: with no ship and no metal, the colony gives one")
{
	const Catalog& catalog = GetCatalog();
	Account account = Fresh();
	std::string why;
	const u32 id = account.ships[0].id;
	REQUIRE(Launch(catalog, account, {0, 1}, {id}, START, why));
	account.resources[Resource::Metal] = 0.0;
	BattleOutcome outcome;
	outcome.node = {0, 1};
	ShipOutcome lost;
	lost.id = id;
	lost.lost = true;
	outcome.ships.push_back(lost);
	ApplyOutcome(catalog, account, outcome, START);
	REQUIRE(account.ships.size() == 1);
	CHECK(account.ships[0].id != id);
	CHECK(account.ships[0].fit.slots[0].id == catalog.colony.rescue.modules[0]);
}

TEST_CASE("account: loot from a node, and JSON both ways")
{
	const Catalog& catalog = GetCatalog();
	u32 random = 7;
	const Loot loot = RollLoot(catalog, NodeAt(catalog, {2, 0}), random);
	CHECK(loot.resources[Resource::Metal] > 0.0);
	f32 volume = 0.0f;
	f32 mass = 0.0f;
	LootSize(catalog, loot, volume, mass);
	CHECK(volume > 0.0f);
	CHECK(mass > 0.0f);

	Account account = Fresh();
	account.cleared = {{1, 0}, {2, -1}};
	account.deepest = 2;
	account.upgradeDone[u32(Building::Fab)] = START + 60;
	account.upgradeDone[u32(Building::Depot)] = START + 90;
	account.ships[0].away = true;
	Account read;
	REQUIRE(FromJson(ToJson(account, true), read));
	CHECK(read.key == "key");
	CHECK(read.name == "Pilot");
	CHECK(read.resources[Resource::Metal] == account.resources[Resource::Metal]);
	CHECK(read.upgradeDone[u32(Building::Fab)] == START + 60);
	CHECK(read.upgradeDone[u32(Building::Depot)] == START + 90);
	CHECK(!read.IsUpgrading(Building::Mine));
	REQUIRE(read.ships.size() == 1);
	CHECK(read.ships[0].fit.slots[0].id == "plasma_s");
	CHECK(Count(read.ships[0].fit.hold, "plasma_charge_s") == 300);
	CHECK(read.ships[0].away);
	CHECK(read.cleared.size() == 2);
	CHECK(read.cleared[1] == Hex{2, -1});
	CHECK(read.modules.size() == 2);
	// Without the key, for the client.
	Account shown;
	REQUIRE(FromJson(ToJson(account, false), shown));
	CHECK(shown.key.empty());
	CHECK(!FromJson("[]", shown));
	CHECK(!FromJson("{\"version\": 99}", shown));
}

TEST_CASE("account: sign-ins, keys and flags in JSON; version 1 still reads")
{
	Account account = Fresh();
	CHECK(account.IsGuest());
	CHECK(account.Provider().size() == 0);
	account.identities.push_back({"steam", "76561198000000001", "Ann"});
	account.keys = {"k2", "k3"};
	account.banned = true;
	account.hidden = true;
	CHECK(!account.IsGuest());
	CHECK(std::string(account.Provider()) == "steam");
	CHECK(account.Opens("key"));
	CHECK(account.Opens("k3"));
	CHECK(!account.Opens("k4"));
	CHECK(!account.Opens(""));
	CHECK(account.FindIdentity("steam"));
	CHECK(!account.FindIdentity("google"));
	Account read;
	REQUIRE(FromJson(ToJson(account, true), read));
	REQUIRE(read.identities.size() == 1);
	CHECK(read.identities[0].id == "76561198000000001");
	CHECK(read.identities[0].name == "Ann");
	CHECK(read.keys == std::vector<std::string>{"k2", "k3"});
	CHECK(read.banned);
	CHECK(read.hidden);
	// The client sees its sign-ins, never the keys.
	Account shown;
	REQUIRE(FromJson(ToJson(account, false), shown));
	CHECK(shown.keys.empty());
	CHECK(shown.identities.size() == 1);

	// Before sign-ins: He-3 was "he4".
	Account old;
	REQUIRE(FromJson(R"({"version": 1, "key": "k", "name": "Old",
		"resources": {"metal": 5, "he4": 7, "chips": 1}})",
	                 old));
	CHECK(old.resources[Resource::He3] == 7.0);
	CHECK(old.resources[Resource::Metal] == 5.0);
	CHECK(old.IsGuest());
	// One upgrade at a time, as accounts kept it then.
	REQUIRE(FromJson(R"({"version": 2, "upgrading": "depot", "upgradeDone": 1790000100})", old));
	CHECK(old.upgradeDone[u32(Building::Depot)] == 1'790'000'100);
}
