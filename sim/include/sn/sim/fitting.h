#pragma once

#include <sn/sim/catalog.h>

#include <string>
#include <vector>

// Ships as accounts keep them, and as the sim flies them (docs/solo-loop.md,
// docs/adr/0013-ships-modules-damage.md): a hull, a module per slot, a hold.
namespace sn::sim
{
// A module in a slot: its id ("" for an empty slot), and its condition, from
// 0 (broken) to 1 (whole).
struct FittedModule
{
	std::string id;
	f32 condition = 1.0f;
};

// A ship as an account keeps it.
struct ShipFit
{
	std::string hull;
	f32 condition = 1.0f; // the hull's share of its hit points
	// Slot by slot: the externals, then the internals, then the rigs, as
	// many as the hull has.
	std::vector<FittedModule> slots;
	std::vector<Stack> hold;
};

// The kinds of a hull's slots, in order.
std::vector<SlotKind> SlotsOf(const HullDesc& hull);
// A fit from a plan (the colony's start, enemies): each module in the first
// free slot of its kind.
ShipFit FitFromPlan(const Catalog& catalog, const ShipPlan& plan);
// The volume and mass of what a hold carries; false for an unknown item.
bool HoldSize(const Catalog& catalog, const std::vector<Stack>& hold, f32& volume, f32& mass);

// The sim's ship for a fit: the hull's numbers with its modules' (mass,
// capacitor), each slot's module (a gun's damage from its ammo; an empty
// slot is ModuleKind::None), the hold's charges for its guns, and the whole
// hold as volume and mass. Hull damage stays; the shield and the capacitor
// start full. False, with why, when the fit does not match the catalog or
// the ship is destroyed.
bool MakeShip(const Catalog& catalog, const ShipFit& fit, Ship& ship, std::string* error = nullptr);
// An enemy made stronger by its ring: hull, shield and damage times
// `strength`.
void Strengthen(Ship& ship, f32 strength);

// A ship's numbers summed up for screens and the website.
struct FitStats
{
	f32 mass = 0.0f;         // t, the hold's included
	f32 acceleration = 0.0f; // m/s^2
	f32 accelerationBack = 0.0f;
	f32 turnRate = 0.0f;  // rad/s
	f32 maxSpeed = 0.0f;  // m/s
	f32 reactor = 0.0f;   // MW
	f32 load = 0.0f;      // MW the modules draw
	f32 capacitor = 0.0f; // GJ
	f32 recharge = 0.0f;  // GJ/s at rest; negative when overloaded
	f32 use = 0.0f;       // GJ/s with every weapon firing and the boosters on
	f32 sustain = 0.0f;   // s of that from a full capacitor; 0: forever
	Damage dps;           // every weapon on target
	f32 range = 0.0f;     // m, the longest weapon's
	f32 hull = 0.0f;
	f32 shield = 0.0f;
	f32 shieldRegen = 0.0f;
	f32 boost = 0.0f; // shield/s from boosters
	f32 shieldThreshold = 0.0f;
	f32 resist[DAMAGE_TYPES] = {};
	f32 scanner = 0.0f;
	f32 cargo = 0.0f;
	f32 cargoUsed = 0.0f;
	u32 ammo = 0; // charges for its guns, the magazines' included

	bool Overloaded() const { return load > reactor; }
};
FitStats StatsOf(const Ship& ship);
} // namespace sn::sim
