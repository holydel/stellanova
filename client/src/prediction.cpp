#include "prediction.h"

#include <utility>

namespace sn
{
using namespace ph;

void Prediction::Reset(const sim::HullClass& hull, std::vector<sim::Rock> field)
{
	now = {};
	now.hull = hull;
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

void Prediction::Fly(const sim::ShipControls& controls)
{
	now.controls = controls;
	sim::FlyShip(now);
	sim::BounceOffRocks(now, rocks.data(), u32(rocks.size()), nullptr, 0);
}

void Prediction::Step(const sim::ShipControls& controls)
{
	pending.push_back({++number, controls});
	if (pending.size() > KEPT)
		pending.erase(pending.begin());
	if (!active)
		return;
	before = now;
	Fly(controls);
}

void Prediction::Correct(const sim::ShipState& state, f32 cargoMass, u32 applied)
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
	now.cargoMass = cargoMass;
	now.controls = state.controls;
	before = now;
	for (const Controls& sent : pending)
	{
		before = now;
		Fly(sent.controls);
	}
	active = true;
}
} // namespace sn
