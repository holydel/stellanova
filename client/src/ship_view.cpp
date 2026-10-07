#include "ship_view.h"

#include "resources.h"

#include <ph/core/profile.h>
#include <ph/render/camera.h>
#include <ph/render/mesh.h>
#include <ph/render/pbr.h>

#include <algorithm>
#include <cmath>

namespace sn
{
namespace
{
using namespace ph;

constexpr f32 ELEVATION = 0.5f;  // radians above the ship
constexpr f32 TURN_RATE = 0.22f; // radians a second
constexpr f32 DISTANCE = 4.0f;   // the ship's radii from the camera
constexpr f32 FILL = 0.42f;      // of the region's shorter side, its radius
} // namespace

void ShipView::Draw(rhi::CommandList& commands, const render::FrameTime& time, Resources& resources,
                    Vec2 min, Vec2 max)
{
	PH_PROFILE_SCOPE("ShipView.Draw");
	const Vec2 screen = {f32(commands.size.width), f32(commands.size.height)};
	const f32 side = std::min(max.x - min.x, max.y - min.y);
	if (side < 8.0f || screen.x < 1.0f || screen.y < 1.0f)
		return;
	Resources::ShipModel& ship = resources.shipModel;
	const f32 radius = std::max(resources.shieldRadius / 1.1f, 0.1f);
	const f32 distance = DISTANCE * radius;
	const f32 angle = f32(time.seconds) * TURN_RATE;
	const Vec3 eye = {distance * std::cos(ELEVATION) * std::sin(angle),
	                  distance * std::sin(ELEVATION),
	                  distance * std::cos(ELEVATION) * std::cos(angle)};
	const Mat4 view = LookAt(eye, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
	// The lens shifted: the camera's axis through the region's middle, the
	// ship's radius over FILL of its shorter side.
	const Vec2 middle = (min + max) * 0.5f;
	const f32 perPixel = radius / (distance * FILL * side);
	const Mat4 projection =
		PerspectiveFromTangents(-middle.x * perPixel, (screen.x - middle.x) * perPixel,
		                        middle.y * perPixel, -(screen.y - middle.y) * perPixel, 0.05f);
	const render::View camera = {projection * view, eye};
	render::FrameData frame = render::MakeFrameData(commands, time, &camera, 1);
	const Vec3 sun = Normalize(Vec3{-0.5f, 0.7f, 0.5f});
	frame.sunDirection[0] = sun.x;
	frame.sunDirection[1] = sun.y;
	frame.sunDirection[2] = sun.z;
	render::SetFrameData(commands, frame);
	if (ship.loaded)
		render::DrawModel(commands, ship.model, resources.light, ship.fit, frame);
	else
		render::DrawMesh(commands, resources.ship, resources.material, Mat4::Identity());
}
} // namespace sn
