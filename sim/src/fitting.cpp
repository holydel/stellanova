#include <sn/sim/fitting.h>

#include <algorithm>
#include <cmath>

namespace sn::sim
{
namespace
{
void Fail(std::string* error, std::string why)
{
	if (error)
		*error = std::move(why);
}

// A module's working numbers; `charge` is a gun's ammo.
ModuleClass ClassOf(const ModuleDesc& desc, const AmmoDesc* charge)
{
	ModuleClass type;
	type.kind = desc.kind;
	type.external = desc.slot == SlotKind::External;
	type.health = desc.health;
	type.power = desc.power;
	type.energy = desc.energy;
	type.range = desc.range;
	type.turnSpeed = desc.turnSpeed;
	type.boost = desc.boost;
	if (desc.kind == ModuleKind::Laser)
		type.damage = desc.dps;
	if (desc.kind == ModuleKind::Plasma && charge)
	{
		type.interval = 1.0f / desc.fireRate;
		type.shotSpeed = desc.shotSpeed;
		type.shotRadius = desc.shotRadius;
		type.reload = desc.reload;
		type.magazine = u32(std::floor(desc.magazine / charge->volume + 1e-3f));
		for (u32 t = 0; t < DAMAGE_TYPES; ++t)
			type.damage.amount[t] = charge->damage.amount[t] * desc.factor[t];
	}
	return type;
}
} // namespace

std::vector<SlotKind> SlotsOf(const HullDesc& hull)
{
	std::vector<SlotKind> slots;
	for (u32 kind = 0; kind < SLOT_KINDS; ++kind)
		slots.insert(slots.end(), hull.slots[kind], SlotKind(kind));
	return slots;
}

ShipFit FitFromPlan(const Catalog& catalog, const ShipPlan& plan)
{
	ShipFit fit;
	fit.hull = plan.hull;
	fit.hold = plan.hold;
	const HullDesc* hull = catalog.FindHull(plan.hull);
	if (!hull)
		return fit;
	const std::vector<SlotKind> kinds = SlotsOf(*hull);
	fit.slots.resize(kinds.size());
	for (const std::string& id : plan.modules)
	{
		const ModuleDesc* module = catalog.FindModule(id);
		for (usize slot = 0; module && slot < kinds.size(); ++slot)
		{
			if (kinds[slot] == module->slot && fit.slots[slot].id.empty())
			{
				fit.slots[slot].id = id;
				break;
			}
		}
	}
	return fit;
}

bool HoldSize(const Catalog& catalog, const std::vector<Stack>& hold, f32& volume, f32& mass)
{
	volume = 0.0f;
	mass = 0.0f;
	for (const Stack& stack : hold)
	{
		f32 itemVolume = 0.0f;
		f32 itemMass = 0.0f;
		if (!catalog.ItemSize(stack.id, itemVolume, itemMass))
			return false;
		volume += itemVolume * f32(stack.count);
		mass += itemMass * f32(stack.count);
	}
	return true;
}

bool MakeShip(const Catalog& catalog, const ShipFit& fit, Ship& ship, std::string* error)
{
	const HullDesc* desc = catalog.FindHull(fit.hull);
	if (!desc)
	{
		Fail(error, "no hull " + fit.hull);
		return false;
	}
	const std::vector<SlotKind> kinds = SlotsOf(*desc);
	if (fit.slots.size() > kinds.size() || fit.condition <= 0.0f)
	{
		Fail(error, "more modules than slots, or a destroyed hull");
		return false;
	}
	ship = {};
	HullClass& hull = ship.hull;
	hull.mass = desc->mass;
	hull.thrust = desc->thrustForward;
	hull.thrustBack = desc->thrustBackward;
	hull.thrustTurn = desc->thrustTurn;
	hull.maxSpeed = desc->maxSpeed;
	hull.drag = catalog.rules.drag;
	hull.radius = desc->radius;
	hull.bounce = catalog.rules.bounce;
	hull.health = desc->hull;
	hull.shield = desc->shield;
	hull.shieldRegen = desc->shieldRegen;
	hull.shieldThreshold = desc->shieldThreshold;
	std::copy(std::begin(desc->resist), std::end(desc->resist), std::begin(hull.resist));
	hull.reactor = desc->reactor;
	hull.capacitor = desc->capacitor;
	hull.scanner = desc->scanner;
	hull.cargo = desc->cargo;
	hull.cargoMassFactor = desc->cargoMassFactor;

	// The guns' ammo: the first gun's kind (one kind a ship for now).
	const AmmoDesc* charge = nullptr;
	ship.moduleCount = u32(kinds.size());
	for (usize slot = 0; slot < fit.slots.size(); ++slot)
	{
		const FittedModule& fitted = fit.slots[slot];
		if (fitted.id.empty())
			continue;
		const ModuleDesc* module = catalog.FindModule(fitted.id);
		if (!module || module->slot != kinds[slot])
		{
			Fail(error, "module " + fitted.id + " cannot go in slot " + std::to_string(slot));
			return false;
		}
		const AmmoDesc* ammo = catalog.FindAmmo(module->ammo);
		if (ammo && !charge)
			charge = ammo;
		Module& sim = ship.modules[slot];
		sim.type = ClassOf(*module, ammo);
		sim.health = sim.type.health * std::clamp(fitted.condition, 0.0f, 1.0f);
		hull.mass += module->mass;
		hull.capacitor += module->capacitor;
	}

	for (const Stack& stack : fit.hold)
	{
		f32 volume = 0.0f;
		f32 mass = 0.0f;
		if (!catalog.ItemSize(stack.id, volume, mass))
		{
			Fail(error, "an unknown item in the hold: " + stack.id);
			return false;
		}
		ship.cargoUsed += volume * f32(stack.count);
		ship.cargoMass += mass * f32(stack.count);
		if (charge && stack.id == charge->id)
		{
			ship.ammo += stack.count;
			ship.ammoVolume = volume;
			ship.ammoMass = mass;
		}
	}
	ship.health = hull.health * std::min(fit.condition, 1.0f);
	ship.shield = hull.shield;
	ship.capacitor = hull.capacitor;
	return true;
}

void Strengthen(Ship& ship, f32 strength)
{
	ship.hull.health *= strength;
	ship.hull.shield *= strength;
	ship.health *= strength;
	ship.shield *= strength;
	for (u32 i = 0; i < ship.moduleCount; ++i)
	{
		for (f32& amount : ship.modules[i].type.damage.amount)
			amount *= strength;
	}
}

FitStats StatsOf(const Ship& ship)
{
	const HullClass& hull = ship.hull;
	FitStats stats;
	stats.mass = MassOf(ship);
	stats.acceleration = hull.thrust / stats.mass;
	stats.accelerationBack = hull.thrustBack / stats.mass;
	stats.turnRate = hull.thrustTurn / stats.mass;
	stats.maxSpeed = hull.maxSpeed;
	stats.reactor = hull.reactor;
	stats.capacitor = hull.capacitor;
	stats.hull = hull.health;
	stats.shield = hull.shield;
	stats.shieldRegen = hull.shieldRegen;
	stats.shieldThreshold = hull.shieldThreshold;
	std::copy(std::begin(hull.resist), std::end(hull.resist), std::begin(stats.resist));
	stats.scanner = hull.scanner;
	stats.cargo = hull.cargo;
	stats.cargoUsed = ship.cargoUsed;
	stats.ammo = ship.ammo;
	for (u32 i = 0; i < ship.moduleCount; ++i)
	{
		const Module& module = ship.modules[i];
		if (module.health <= 0.0f)
			continue;
		const ModuleClass& type = module.type;
		stats.load += type.power;
		stats.ammo += module.loaded;
		switch (type.kind)
		{
			case ModuleKind::Laser:
				stats.use += type.energy;
				for (u32 t = 0; t < DAMAGE_TYPES; ++t)
					stats.dps.amount[t] += type.damage.amount[t];
				stats.range = std::max(stats.range, type.range);
				break;
			case ModuleKind::Plasma:
				stats.use += type.energy / type.interval;
				for (u32 t = 0; t < DAMAGE_TYPES; ++t)
					stats.dps.amount[t] += type.damage.amount[t] / type.interval;
				stats.range = std::max(stats.range, type.range);
				break;
			case ModuleKind::ShieldBooster:
				stats.use += type.energy;
				stats.boost += type.boost;
				break;
			case ModuleKind::CapacitorBattery:
			case ModuleKind::None: break;
		}
	}
	stats.recharge = (stats.reactor - stats.load) * 0.001f;
	if (stats.use > stats.recharge)
		stats.sustain = stats.capacitor / (stats.use - stats.recharge);
	return stats;
}
} // namespace sn::sim
