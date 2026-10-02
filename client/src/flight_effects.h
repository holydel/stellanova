#pragma once

#include <ph/assets/mesh.h>
#include <ph/core/math.h>
#include <ph/render/effects.h>
#include <ph/rhi/rhi.h>

#include <vector>

// How flying ships look beyond their models: flame from their engines while
// they thrust, puffs at their noses while they reverse, trails behind their
// engines, shields lighting up where they are hit, smoke from damaged hulls;
// and sparks, explosions and the flash of ships arriving. The client's own,
// drawn with pith's effects (ph/render/effects.h): the sim knows none of it.
namespace sn
{
struct Resources;

// A kind of ship's looks: where its engines are (from its model's bounds:
// the nose along -Z, the engines at the back), its shield, its colors
// (0xAABBGGRR, straight alpha).
struct ShipLook
{
	ph::assets::MeshBounds bounds;
	ph::f32 shieldRadius = 2.0f;
	ph::u32 flameHot = 0;     // flame as it leaves an engine...
	ph::u32 flameCold = 0;    // ...and as it fades
	ph::u32 core = 0;         // an engine's hot core (its alpha is the full burn's)
	ph::Vec3 trail;           // the trails' color
	ph::Vec3 shield;          // the shield's color
	ph::f32 flameRate = 1.0f; // a share of the full flame
};

class FlightEffects
{
public:
	void Reset();
	// Each frame: Begin, then Ship for each ship drawn, then Draw. Ships
	// not given in a frame leave their trails to fade.
	void Begin(ph::f32 dt);
	// Ship `id` with its model's transform, its velocity in the world, its
	// thrust (-1 reverse to 1 forward) and its damage (0 whole to 1 broken).
	void Ship(ph::u32 id, const ShipLook& look, const ph::Mat4& model, ph::Vec3 velocity,
	          ph::f32 thrust, ph::f32 damage);
	// Ship `id`'s shield lights up toward `direction` (in the world), more
	// for a harder hit (`strength`, m/s of a bump).
	void Bump(ph::u32 id, ph::Vec3 direction, ph::f32 strength);
	void Sparks(ph::Vec3 at, ph::u32 count = 8);
	// A ship breaking apart; `size` is about its radius.
	void Explosion(ph::Vec3 at, ph::f32 size);
	// A ship arriving: a flash and a ring in its colors.
	void WarpIn(ph::Vec3 at, ph::Vec3 color);
	// After the opaque scene and the sky.
	void Draw(ph::rhi::CommandList& commands, const Resources& resources,
	          const ph::render::CameraAxes& camera);

private:
	struct Particle
	{
		ph::Vec3 position;
		ph::Vec3 velocity;
		ph::f32 age = 0.0f;
		ph::f32 life = 1.0f;
		ph::f32 startSize = 0.2f;
		ph::f32 endSize = 0.5f;
		ph::u32 startColor = 0;
		ph::u32 endColor = 0;
	};

	struct TrailPoint
	{
		ph::Vec3 position;
		ph::f32 age = 0.0f;
		ph::f32 strength = 0.0f; // 0 to 1: how fast the ship went
	};

	struct Hit
	{
		ph::Vec3 direction;
		ph::f32 age = 0.0f;
		ph::f32 strength = 1.0f;
	};

	// A ship's own effects, kept between frames.
	struct Looks
	{
		ph::u32 id = 0;
		ShipLook look;
		bool seen = false;   // given this frame
		ph::Vec3 center;     // in the world, this frame
		ph::Vec3 nozzles[2]; // in the world, this frame
		ph::f32 burn = 0.0f; // 0 to 1: how hard the engines push forward
		ph::f32 flicker = 1.0f;
		ph::f32 owedFlame = 0.0f; // particles due but not yet made
		ph::f32 owedPuffs = 0.0f;
		ph::f32 owedSmoke = 0.0f;
		std::vector<TrailPoint> trails[2]; // behind the left and right engine
		std::vector<Hit> hits;
	};

	ph::f32 Random();
	Looks* Find(ph::u32 id);
	// Into `particles` (glows, drawn additively) or `smoke` (drawn over
	// the scene, darkening it).
	void Emit(std::vector<Particle>& into, ph::Vec3 at, ph::Vec3 velocity, ph::f32 life,
	          ph::f32 startSize, ph::f32 endSize, ph::u32 startColor, ph::u32 endColor);

	std::vector<Particle> particles;
	std::vector<Particle> smoke;
	std::vector<Looks> ships;
	ph::f32 dt = 0.0f;
	ph::u32 seed = 1;
	std::vector<ph::render::EffectVertex> vertices; // reused each frame
	std::vector<ph::render::RibbonPoint> ribbon;
};
} // namespace sn
