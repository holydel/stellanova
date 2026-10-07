#pragma once

#include <sn/sim/account.h>

#include <ph/core/math.h>
#include <ph/render/frame.h>
#include <ph/rhi/rhi.h>

// The colony as a scene, behind the hub's Colony tab: its five buildings in
// an arc that follows the cards below them, on a dark moon under its gas
// giant, the camera swaying slowly. A building grows a little with its
// levels, glows gold while it is upgraded, and lights up under the pointer
// or when picked. Kenney's models stand in until the generated ones
// (docs/content-generation.md).
namespace sn
{
struct Resources;

class ColonyView
{
public:
	// The scene, before the interface: `hovered` and `picked` are buildings
	// or -1. The colony fits `regionMin`-`regionMax` (pixels: the part of the
	// screen above its cards, or on a phone above their list); empty: the
	// whole screen.
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time,
	          Resources& resources, const sim::Account& shown, ph::i32 hovered, ph::i32 picked,
	          ph::Vec2 regionMin, ph::Vec2 regionMax);
	// From the last Draw: a building's foot on the screen (pixels) and its
	// height there; false when it is behind the camera.
	bool Project(ph::u32 building, ph::Vec2& foot, ph::f32& height) const;
	// The building under a point on the screen (pixels), or -1.
	ph::i32 Pick(ph::Vec2 pixel) const;

private:
	ph::Mat4 viewProjection;
	ph::Vec2 size;         // the screen, pixels
	ph::Vec3 base[5] = {}; // each building's foot, in the world
	ph::f32 tall[5] = {};  // and its height, meters
	bool projected = false;
};
} // namespace sn
