#pragma once

#include <sn/sim/world.h>

// Bots that fly ships (docs/adr/0009-bots-v0.md): each tick a pilot looks at
// the world and asks its ship for what a player's controls would: turn,
// thrust, fire. It makes attack runs: at its target, firing where its shots
// meet it, then away when close, and around for another pass. On its way it
// steers clear of rocks and keeps apart from its own team. Plain data and
// functions, the same each run for the same world: the server runs pilots
// for the match's bots.
namespace sn::bots
{
using sim::f32;
using sim::u32;
using sim::u8;
using sim::Vec2;

// How good a pilot is.
struct PilotSkill
{
	f32 aimError = 0.08f; // rad: how far its aim strays from the right lead
	f32 reaction = 0.4f;  // s between looks for a nearer target
	f32 sight = 220.0f;   // m: how far it sees ships
	f32 range = 55.0f;    // m: it fires within this
	f32 closest = 9.0f;   // m: nearer than this to its target, it breaks away
};

// A pilot's memory between ticks.
struct Pilot
{
	enum class Pass : u8
	{
		Attack, // at the target
		Away,   // away from it, to come around again
	};

	sim::ShipHandle target;
	Pass pass = Pass::Attack;
	f32 passLeft = 0.0f;  // s until Away turns back to Attack
	f32 away = 0.0f;      // rad: the heading of Away
	f32 untilLook = 0.0f; // s until it looks for a target again
	f32 untilAim = 0.0f;  // s until its aim strays somewhere else
	f32 stray = 0.0f;     // rad: where its aim strays now
	u32 random = 1;       // seeds its own numbers
	Vec2 roam;            // where it goes while it sees no one
};

// This tick's controls for the ship that `pilot` flies; none for a wreck.
sim::ShipControls Fly(const sim::World& world, sim::ShipHandle ship, Pilot& pilot,
                      const PilotSkill& skill);

// Where to aim from `from`, moving at `velocity`, so that a shot at `speed`
// (on top of that velocity) meets a target at `at` moving at
// `targetVelocity`; `at` itself when no shot can catch it.
Vec2 Lead(Vec2 from, Vec2 velocity, f32 speed, Vec2 at, Vec2 targetVelocity);
} // namespace sn::bots
