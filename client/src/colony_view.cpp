#include "colony_view.h"

#include "resources.h"

#include <ph/core/profile.h>
#include <ph/render/camera.h>
#include <ph/render/material.h>
#include <ph/render/mesh.h>
#include <ph/render/pbr.h>
#include <ph/render/sky.h>

#include <algorithm>
#include <cmath>

namespace sn
{
namespace
{
using namespace ph;

static_assert(COLONY_BUILDINGS == sim::BUILDINGS);

constexpr f32 DISTANCE = 44.0f; // m from the camera to what it looks at
constexpr f32 ELEVATION = 0.2f; // radians above the ground: the horizon stays in sight
// The lens is shifted so that what the camera looks at is the middle of its
// region, zoomed so that the cluster fits it: this wide and this tall, as
// tangents from the camera's axis (half of each).
constexpr f32 HALF_WIDTH = 0.72f;
constexpr f32 HALF_HEIGHT = 0.29f;
constexpr f32 SWAY = 0.16f;      // radians each way, over about a minute and a half
constexpr f32 SWAY_RATE = 0.07f; // radians a second
constexpr f32 GROWTH = 0.06f;    // more size a level
constexpr u32 GROWN_LEVELS = 6;  // past these, no bigger
constexpr u32 ROCKS = 70;

// Where each building stands (meters; x right, z toward the camera): a
// cluster on pads around a junction, as in the colony's concept (SCN1): the
// mine at the back on the left, the extractor at the back in the middle, the
// command center at the back on the right, the fab and the depot in front;
// and its size across.
constexpr Vec3 PLACES[sim::BUILDINGS] = {{-19.0f, 0.0f, -7.0f},
                                         {0.0f, 0.0f, -14.0f},
                                         {-7.0f, 0.0f, 7.0f},
                                         {7.0f, 0.0f, 7.0f},
                                         {19.0f, 0.0f, -7.0f}};
constexpr f32 SIZES[sim::BUILDINGS] = {6.0f, 5.0f, 6.5f, 8.0f, 7.0f};
constexpr f32 PAD_HEIGHT = 0.35f; // m: the buildings stand on it
// A generated building (Resources::colonyModels) brings its own pad: this
// much wider than SIZES, turned so that its concept's corner faces us.
constexpr f32 MODEL_ACROSS = 1.2f;
constexpr f32 MODEL_TURN = 0.7854f;
constexpr f32 PICKED_GLOW[4] = {0.05f, 0.16f, 0.24f, 0.0f}; // as Resources::litMaterial
// The generated crater's disc is this much wider than its bowl; its flat
// ring lies this far up its height.
constexpr f32 CRATER_ACROSS = 1.8f;
constexpr f32 CRATER_RING = 0.7f;

// The rest of the colony: the dish by the command center, craters, the
// landing pad in front.
struct Prop
{
	u32 mesh;
	Vec3 at;
	f32 size;
	f32 yaw;
};
constexpr Prop PROPS[] = {{0, {25.0f, 0.0f, -11.0f}, 4.0f, 0.6f},
                          {1, {-32.0f, 0.0f, -22.0f}, 16.0f, 0.0f},
                          {1, {36.0f, 0.0f, -30.0f}, 12.0f, 1.0f},
                          {1, {-30.0f, 0.0f, 10.0f}, 6.0f, 2.0f},
                          {2, {0.0f, 0.0f, 0.5f}, 4.5f, 0.0f}};

f32 Across(const assets::MeshBounds& bounds)
{
	return std::max({bounds.max.x - bounds.min.x, bounds.max.z - bounds.min.z, 0.01f});
}

// Steady pseudo-random numbers: the same rocks every frame.
f32 Random(u32& state)
{
	state = state * 1664525u + 1013904223u;
	return f32(state >> 8) / f32(1u << 24);
}
} // namespace

void ColonyView::Draw(rhi::CommandList& commands, const render::FrameTime& time,
                      Resources& resources, const sim::Account& shown, i32 hovered, i32 picked,
                      Vec2 regionMin, Vec2 regionMax)
{
	PH_PROFILE_SCOPE("Colony.Draw");
	size = {f32(commands.size.width), f32(commands.size.height)};
	const f32 t = f32(time.seconds);
	const Vec3 target = {0.0f, 2.5f, 0.0f};
	const f32 yaw = SWAY * std::sin(t * SWAY_RATE);
	const Vec3 eye = target + Vec3{DISTANCE * std::cos(ELEVATION) * std::sin(yaw),
	                               DISTANCE * std::sin(ELEVATION),
	                               DISTANCE * std::cos(ELEVATION) * std::cos(yaw)};
	const Mat4 view = LookAt(eye, target, {0.0f, 1.0f, 0.0f});
	if (regionMax.x - regionMin.x < 8.0f || regionMax.y - regionMin.y < 8.0f)
	{
		regionMin = {0.0f, 0.0f};
		regionMax = size;
	}
	const Vec2 axis = (regionMin + regionMax) * 0.5f;
	const f32 perPixel = std::max(HALF_WIDTH / (0.5f * (regionMax.x - regionMin.x)),
	                              HALF_HEIGHT / (0.5f * (regionMax.y - regionMin.y)));
	const Mat4 projection =
		PerspectiveFromTangents(-axis.x * perPixel, (size.x - axis.x) * perPixel, axis.y * perPixel,
		                        -(size.y - axis.y) * perPixel, 0.05f);
	viewProjection = projection * view;
	const render::View camera = {viewProjection, eye};
	render::FrameData frame = render::MakeFrameData(commands, time, &camera, 1);
	// A low sun from the right and behind: rims and long shades.
	const Vec3 sun = Normalize(Vec3{0.75f, 0.42f, -0.5f});
	frame.sunDirection[0] = sun.x;
	frame.sunDirection[1] = sun.y;
	frame.sunDirection[2] = sun.z;
	frame.ambient[0] = 0.07f;
	frame.ambient[1] = 0.08f;
	frame.ambient[2] = 0.11f;
	render::SetFrameData(commands, frame);
	projected = true;
	// The sky first, though the scene hides much of it: on the AGM (Adreno
	// 619, its 2021 driver) a pass whose first draw writes depth drew none of
	// its depth-tested draws, and some frames showed other tiles' pictures
	// (pith's docs/targets/android.md). The sky tests depth without writing it.
	if (resources.sky)
		render::DrawSky(commands, resources.sky, frame, 1.0f);
	render::DrawMesh(commands, resources.ground, resources.groundMaterial, Mat4::Identity());
	for (const Prop& prop : PROPS)
	{
		// The generated crater: its bowl as wide as the prop, its flat ring
		// around level with the ground.
		if (prop.mesh == 1 && resources.craterModeled)
		{
			const assets::MeshBounds& bounds = resources.crater.bounds;
			const f32 scale = CRATER_ACROSS * prop.size / Across(bounds);
			const Vec3 middle = (bounds.min + bounds.max) * 0.5f;
			const f32 sunk = bounds.min.y + CRATER_RING * (bounds.max.y - bounds.min.y);
			render::DrawModel(commands, resources.crater, resources.light,
			                  Translation(prop.at - Vec3{0.0f, sunk * scale, 0.0f}) *
			                      RotationY(prop.yaw) * Scale({scale, scale, scale}) *
			                      Translation({-middle.x, 0.0f, -middle.z}),
			                  frame);
			continue;
		}
		const assets::MeshBounds& bounds = resources.colonyPropBounds[prop.mesh];
		const f32 scale = prop.size / Across(bounds);
		// Craters are the ground's color; the dish and the pad are the colony's.
		render::DrawMesh(commands, resources.colonyProps[prop.mesh],
		                 prop.mesh == 1 ? resources.boulderMaterial : resources.material,
		                 Translation(prop.at + Vec3{0.0f, -bounds.min.y * scale, 0.0f}) *
		                     RotationY(prop.yaw) * Scale({scale, scale, scale}));
	}
	// The roads from the junction to each pad, and the pads.
	for (u32 b = 0; b < sim::BUILDINGS; ++b)
	{
		const Vec3 to = PLACES[b];
		const f32 length = std::sqrt(to.x * to.x + to.z * to.z);
		const f32 heading = std::atan2(to.x, to.z);
		render::DrawMesh(commands, resources.bolt, resources.roadMaterial,
		                 RotationY(heading) * Translation({0.0f, 0.05f, 0.5f * length}) *
		                     Scale({1.6f, 0.1f, length}));
		if (resources.colonyModeled[b])
			continue;
		const f32 radius = 0.62f * SIZES[b];
		render::DrawMesh(commands, resources.pad, resources.padMaterial,
		                 Translation(to + Vec3{0.0f, 0.5f * PAD_HEIGHT, 0.0f}) *
		                     RotationY(0.3927f) * Scale({radius, PAD_HEIGHT, radius}));
	}

	// Rocks strewn around, never on the cluster.
	u32 seed = 20261004;
	for (u32 i = 0; i < ROCKS; ++i)
	{
		const f32 angle = 6.2831853f * Random(seed);
		const f32 distance = 28.0f + 110.0f * Random(seed) * Random(seed);
		const f32 across = 0.6f + 2.4f * Random(seed) * Random(seed);
		const u32 mesh = u32(Random(seed) * ROCK_MESHES) % ROCK_MESHES;
		const f32 spin = 6.2831853f * Random(seed);
		const f32 scale = across / resources.rockExtents[mesh];
		render::DrawMesh(commands, resources.rocks[mesh], resources.boulderMaterial,
		                 Translation({distance * std::cos(angle), 0.3f * across,
		                              distance * std::sin(angle) - 10.0f}) *
		                     RotationY(spin) * Scale({scale, 0.6f * scale, scale}));
	}

	// The buildings: a little bigger each level; gold while upgraded.
	const f32 pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
	const f32 gold[4] = {0.22f * pulse, 0.15f * pulse, 0.03f * pulse, 1.0f};
	render::SetMaterialValue(resources.busyMaterial, "emissive", gold, sizeof(gold));
	for (u32 b = 0; b < sim::BUILDINGS; ++b)
	{
		const u32 grown = std::min(shown.levels[b], GROWN_LEVELS + 1) - 1;
		const f32 growth = 1.0f + GROWTH * f32(grown);
		const bool busy = shown.IsUpgrading(sim::Building(b));
		const bool lit = i32(b) == hovered || i32(b) == picked;
		// Each turned a little toward the junction.
		const f32 facing = -0.04f * PLACES[b].x;
		if (resources.colonyModeled[b])
		{
			render::Model& model = resources.colonyModels[b];
			const assets::MeshBounds& bounds = model.bounds;
			const f32 scale = MODEL_ACROSS * SIZES[b] * growth / Across(bounds);
			const Vec3 middle = (bounds.min + bounds.max) * 0.5f;
			base[b] = PLACES[b];
			tall[b] = (bounds.max.y - bounds.min.y) * scale;
			// Its own glow stays (an emissive map, or none); it brightens instead.
			const f32 tint[4] = {1.0f + 4.0f * (busy  ? gold[0]
			                                    : lit ? PICKED_GLOW[0]
			                                          : 0.0f),
			                     1.0f + 4.0f * (busy  ? gold[1]
			                                    : lit ? PICKED_GLOW[1]
			                                          : 0.0f),
			                     1.0f + 4.0f * (busy  ? gold[2]
			                                    : lit ? PICKED_GLOW[2]
			                                          : 0.0f),
			                     1.0f};
			for (render::Material& material : model.materials)
				render::SetMaterialValue(material, "baseColor", tint, sizeof(tint));
			render::DrawModel(commands, model, resources.light,
			                  Translation(PLACES[b] + Vec3{0.0f, -bounds.min.y * scale, 0.0f}) *
			                      RotationY(facing + MODEL_TURN) * Scale({scale, scale, scale}) *
			                      Translation({-middle.x, 0.0f, -middle.z}),
			                  frame);
			continue;
		}
		const assets::MeshBounds& bounds = resources.colonyBounds[b];
		const f32 scale = SIZES[b] * growth / Across(bounds);
		base[b] = PLACES[b] + Vec3{0.0f, PAD_HEIGHT, 0.0f};
		tall[b] = (bounds.max.y - bounds.min.y) * scale;
		render::Material& material = busy  ? resources.busyMaterial
		                             : lit ? resources.litMaterial
		                                   : resources.material;
		render::DrawMesh(
			commands, resources.colony[b], material,
			Translation(PLACES[b] + Vec3{0.0f, PAD_HEIGHT - bounds.min.y * scale, 0.0f}) *
				RotationY(facing) * Scale({scale, scale, scale}));
	}

	// The gas giant, far off and big in the sky, lit from the side.
	render::DrawMesh(commands, resources.planet, resources.planetMaterial,
	                 Translation({-700.0f, 120.0f, -2600.0f}) * Scale({280.0f, 280.0f, 280.0f}));
}

bool ColonyView::Project(u32 building, Vec2& foot, f32& height) const
{
	if (!projected || building >= sim::BUILDINGS)
		return false;
	const auto onScreen = [this](Vec3 world, Vec2& pixel)
	{
		const Vec4 clip = viewProjection * Vec4{world.x, world.y, world.z, 1.0f};
		if (clip.w <= 0.01f)
			return false;
		pixel = {(0.5f + 0.5f * clip.x / clip.w) * size.x,
		         (0.5f - 0.5f * clip.y / clip.w) * size.y};
		return true;
	};
	Vec2 top;
	if (!onScreen(base[building], foot) ||
	    !onScreen(base[building] + Vec3{0.0f, tall[building], 0.0f}, top))
		return false;
	height = foot.y - top.y;
	return true;
}

i32 ColonyView::Pick(Vec2 pixel) const
{
	i32 best = -1;
	f32 nearest = 1e9f;
	for (u32 b = 0; b < sim::BUILDINGS; ++b)
	{
		Vec2 foot;
		f32 height = 0.0f;
		if (!Project(b, foot, height))
			continue;
		// About as wide as tall on the screen, a margin around.
		const f32 half = std::max(0.55f * height, 24.0f);
		const Vec2 middle = {foot.x, foot.y - 0.5f * height};
		const f32 dx = std::abs(pixel.x - middle.x);
		const f32 dy = std::abs(pixel.y - middle.y);
		if (dx > half + 12.0f || dy > 0.5f * height + 16.0f)
			continue;
		const f32 distance = dx + dy;
		if (distance < nearest)
		{
			nearest = distance;
			best = i32(b);
		}
	}
	return best;
}
} // namespace sn
