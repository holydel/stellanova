#pragma once

#include <sn/sim/world.h>

// Orders (docs/adr/0013-ships-modules-damage.md): what a ship is told to do,
// turned into a tick's controls. Turrets aim and fire on their own, so an
// order only moves the ship: to a point, where it stops; around a target, at
// a distance its weapons reach; or to a stop. A player's own ship (on the
// client, so that its flight is still predicted), its escorts and the bots
// all fly by it.
namespace sn::bots
{
using sim::f32;
using sim::u32;
using sim::u8;
using sim::Vec2;

struct Order
{
	enum class Kind : u8
	{
		Stop,   // slow to a stop
		Move,   // fly to `point` and stop there
		Attack, // circle the target at a distance (Steer's `target`)
	};
	Kind kind = Kind::Stop;
	Vec2 point;
};

// What a pilot knows of the ship it attacks.
struct Mark
{
	Vec2 position;
	Vec2 velocity;
};

// This tick's controls for `ship` to follow `order`. An Attack needs
// `target` (else it stops), and circles it at `distance`. Rocks on the way
// are steered around; `push` adds a wish of the caller's (keeping apart
// from others), as a velocity.
sim::ShipControls Steer(const sim::Ship& ship, const Order& order, const Mark* target, f32 distance,
                        const sim::Rock* rocks, u32 rockCount, Vec2 push = {});

// Where a ship circles its target: this share of its weapons' reach.
f32 AttackDistance(const sim::Ship& ship, f32 share);
} // namespace sn::bots
