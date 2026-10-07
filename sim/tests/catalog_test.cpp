#include <sn/sim/catalog.h>
#include <sn/sim/fitting.h>

#include <doctest/doctest.h>

using namespace sn::sim;

TEST_CASE("catalog: the game's catalog reads, with what the MVP needs")
{
	const Catalog& catalog = GetCatalog();
	CHECK(catalog.hash != 0);
	const HullDesc* lancer = catalog.FindHull("lancer");
	REQUIRE(lancer);
	CHECK(lancer->size == Size::S);
	CHECK(lancer->slots[u32(SlotKind::External)] == 1);
	CHECK(lancer->slots[u32(SlotKind::Internal)] == 1);
	CHECK(catalog.FindModule("laser_s"));
	CHECK(catalog.FindModule("plasma_s"));
	CHECK(catalog.FindAmmo("plasma_charge_s"));
	CHECK(!catalog.FindHull("nothing"));
	CHECK(catalog.rules.combat.beamAim == doctest::Approx(3.0f * 3.14159265f / 180.0f));
	CHECK(!catalog.colony.startShips.empty());
	CHECK(catalog.colony.buildings[u32(Building::Depot)].storage > 0.0f);
	f32 volume = 0.0f;
	f32 mass = 0.0f;
	CHECK(catalog.ItemSize("metal", volume, mass));
	CHECK(volume > 0.0f);
	CHECK(catalog.ItemSize("laser_s", volume, mass));
	CHECK(!catalog.ItemSize("nothing", volume, mass));
}

TEST_CASE("catalog: its hash ignores line endings, and bad references are refused")
{
	CHECK(HashCatalogText("{\"a\": 1,\r\n\"b\": 2}") == HashCatalogText("{\"a\": 1,\n\"b\": 2}"));
	CHECK(HashCatalogText("{\"a\": 1}") != HashCatalogText("{\"a\": 2}"));
	Catalog catalog;
	std::string error;
	CHECK(!ParseCatalog("not json", catalog, error));
	CHECK(!error.empty());
	CHECK(!ParseCatalog(R"({"modules": [{"id": "x", "kind": "laser", "slot": "external",
		"size": "S", "health": 1}]})",
	                    catalog, error));
	CHECK(error.find("colony") != std::string::npos); // no colony, no start
	CHECK(!ParseCatalog(R"({"modules": [{"id": "x", "kind": "warp", "slot": "external",
		"size": "S", "health": 1}]})",
	                    catalog, error));
	CHECK(error.find("kind") != std::string::npos);
}

TEST_CASE("fitting: the Lancer with a plasma gun and a booster")
{
	const Catalog& catalog = GetCatalog();
	ShipPlan plan;
	plan.hull = "lancer";
	plan.modules = {"shield_booster_s", "plasma_s"}; // any order: each to its kind of slot
	plan.hold = {{"plasma_charge_s", 300}};
	const ShipFit fit = FitFromPlan(catalog, plan);
	REQUIRE(fit.slots.size() == 2);
	CHECK(fit.slots[0].id == "plasma_s");
	CHECK(fit.slots[1].id == "shield_booster_s");
	Ship ship;
	std::string error;
	REQUIRE(MakeShip(catalog, fit, ship, &error));
	CHECK(ship.moduleCount == 2);
	CHECK(ship.hull.mass == doctest::Approx(120.0f + 9.0f + 4.0f));
	CHECK(ship.ammo == 300);
	CHECK(ship.cargoUsed == doctest::Approx(3.0f));
	CHECK(ship.cargoMass == doctest::Approx(3.0f));
	CHECK(ship.health == 400.0f);
	CHECK(ship.shield == 300.0f);
	const Module& gun = ship.modules[0];
	CHECK(gun.type.magazine == 30);
	CHECK(gun.type.interval == doctest::Approx(1.0f / 3.0f));
	// 5 kinetic x 1.0 and 5 thermal x 1.2 a shot.
	CHECK(gun.type.damage.amount[u32(DamageType::Kinetic)] == doctest::Approx(5.0f));
	CHECK(gun.type.damage.amount[u32(DamageType::Thermal)] == doctest::Approx(6.0f));

	const FitStats stats = StatsOf(ship);
	CHECK(stats.mass == doctest::Approx(136.0f));
	CHECK(stats.acceleration == doctest::Approx(3600.0f / 136.0f));
	CHECK(stats.dps.Total() == doctest::Approx(33.0f));
	CHECK(stats.load == 30.0f);
	CHECK(stats.recharge == doctest::Approx(0.07f));
	CHECK(stats.boost == 15.0f);
	CHECK(stats.range == 100.0f);
	CHECK(!stats.Overloaded());
	// The gun's 0.018 GJ/s and the booster's 0.05 stay within the 0.07 a
	// second the reactor spares: it can fire and boost forever.
	CHECK(stats.use == doctest::Approx(0.068f));
	CHECK(stats.sustain == 0.0f);
}

TEST_CASE("fitting: a battery adds capacitor; wrong slots and wrecks are refused")
{
	const Catalog& catalog = GetCatalog();
	ShipFit fit;
	fit.hull = "lancer";
	fit.slots = {{"laser_s"}, {"capacitor_battery_s"}};
	Ship ship;
	REQUIRE(MakeShip(catalog, fit, ship));
	CHECK(ship.hull.capacitor == doctest::Approx(3.5f));
	CHECK(StatsOf(ship).dps.amount[u32(DamageType::Em)] == 24.0f);

	fit.slots = {{"capacitor_battery_s"}, {"laser_s"}};
	std::string error;
	CHECK(!MakeShip(catalog, fit, ship, &error));
	CHECK(!error.empty());
	fit.slots = {{"laser_s"}, {""}, {""}};
	CHECK(!MakeShip(catalog, fit, ship));
	fit.slots = {{"laser_s"}};
	fit.condition = 0.0f;
	CHECK(!MakeShip(catalog, fit, ship));

	// Damage stays: a hull at half, a broken module.
	fit.condition = 0.5f;
	fit.slots = {{"laser_s", 0.0f}, {""}};
	REQUIRE(MakeShip(catalog, fit, ship));
	CHECK(ship.health == 200.0f);
	CHECK(ship.modules[0].health == 0.0f);
	CHECK(ship.modules[1].type.kind == ModuleKind::None);
	CHECK(StatsOf(ship).dps.Total() == 0.0f);
}
