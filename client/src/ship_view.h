#pragma once

#include <ph/core/math.h>
#include <ph/render/frame.h>
#include <ph/rhi/rhi.h>

// A ship in 3D in a part of the screen, turning slowly under the colony's
// light: the fitting screen's middle, inside its ring of slots. Our ships'
// generated model where the art pack has it, else Kenney's.
namespace sn
{
struct Resources;

class ShipView
{
public:
	// Before the interface, in the frame's pass: the ship centered on
	// `min`-`max` (pixels), as large as it fits.
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time,
	          Resources& resources, ph::Vec2 min, ph::Vec2 max);
};
} // namespace sn
