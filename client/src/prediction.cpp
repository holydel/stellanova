#include "prediction.h"

#include <utility>

namespace sn
{
using namespace ph;

void Prediction::Reset(const sim::HullClass& hull, const sim::WeaponClass& weapon,
                       std::vector<sim::Rock> field)
{
	now = {};
	now.hull = hull;
	now.weapon = weapon;
	now.health = hull.health;
	before = now;
	rocks = std::move(field);
	pending.clear();
	number = 0;
	active = false;
}

void Prediction::BreakRock(u32 rock)
{
	if (rock < rocks.size())
		rocks[rock].health = 0.0f;
}

bool Prediction::Fly(const sim::ShipControls& controls, sim::Shot& shot)
{
	now.controls = controls;
	sim::FlyShip(now);
	const bool fired = sim::FireGun(now, shot);
	sim::BounceOffRocks(now, rocks.data(), u32(rocks.size()), nullptr, 0);
	return fired;
}

bool Prediction::Step(const sim::ShipControls& controls, sim::Shot& shot)
{
	pending.push_back({++number, controls});
	if (pending.size() > KEPT)
		pending.erase(pending.begin());
	if (!active)
		return false;
	before = now;
	return Fly(controls, shot);
}

void Prediction::Correct(const sim::ShipState& state, f32 cooldown, u32 applied)
{
	std::erase_if(pending, [applied](const Controls& sent) { return sent.number <= applied; });
	if (state.health == 0)
	{
		active = false;
		return;
	}
	now.position = state.position;
	now.velocity = state.velocity;
	now.angle = state.angle;
	now.cooldown = cooldown;
	now.controls = state.controls;
	before = now;
	sim::Shot unused;
	for (const Controls& sent : pending)
	{
		before = now;
		Fly(sent.controls, unused);
	}
	active = true;
}
} // namespace sn
