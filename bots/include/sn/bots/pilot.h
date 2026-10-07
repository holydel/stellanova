#pragma once

#include <sn/bots/orders.h>

// Bots that fly ships (docs/adr/0009-bots-v0.md, 0013-ships-modules-damage.md):
// each tick a pilot looks at the world, picks a target, and steers as an
// Attack order would: close to its weapons' reach and around the target,
// while its turrets fire on their own. Without a target in sight it roams.
// On its way it steers clear of rocks and keeps apart from its own team.
// Plain data and functions, the same each run for the same world: the
// server runs pilots for the match's bots and escorts.
namespace sn::bots
{
// How a pilot sees and fights.
struct PilotSkill
{
	f32 sight = 260.0f;  // m: how far it sees ships
	f32 reaction = 0.4f; // s between looks for a nearer target
	f32 distance = 0.8f; // of its weapons' reach, where it circles its target
};

// A pilot's memory between ticks.
struct Pilot
{
	sim::ShipHandle target; // the server tells the ship (sim::SetTarget)
	f32 untilLook = 0.0f;   // s until it looks for a target again
	u32 random = 1;         // seeds its own numbers
	Vec2 roam;              // where it goes while it sees no one
};

// This tick's controls for the ship that `pilot` flies; none for a wreck.
sim::ShipControls Fly(const sim::World& world, sim::ShipHandle ship, Pilot& pilot,
                      const PilotSkill& skill);

// Away from ships of its own team that come too near, as a velocity.
Vec2 KeepApart(const sim::World& world, sim::ShipHandle handle);
} // namespace sn::bots
