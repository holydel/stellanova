#include "flight_effects.h"

#include "resources.h"

#include <ph/render/canvas.h>

#include <algorithm>
#include <cmath>

namespace sn
{
namespace
{
using namespace ph;
using render::PackColor;

constexpr u32 MAX_PARTICLES = 4096;
constexpr u32 MAX_SMOKE = 1024;
constexpr f32 FLAME_RATE = 520.0f; // particles a second from each engine at full thrust
constexpr f32 PUFF_RATE = 90.0f;   // from each side of the nose at full reverse
constexpr f32 SMOKE_RATE = 40.0f;  // from a hull about to break
constexpr f32 TRAIL_SECONDS = 1.0f;
constexpr u32 MAX_TRAIL_POINTS = 256;
constexpr f32 TRAIL_SPEED = 40.0f; // m/s at which a trail is at its brightest
constexpr f32 HIT_SECONDS = 0.5f;  // how long a shield glows after a hit

Vec3 Point(const Mat4& m, Vec3 p)
{
	const Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0f};
	return {r.x, r.y, r.z};
}

Vec3 Direction(const Mat4& m, Vec3 d)
{
	const Vec4 r = m * Vec4{d.x, d.y, d.z, 0.0f};
	return Normalize(Vec3{r.x, r.y, r.z});
}

u32 Channel(u32 color, u32 shift) { return (color >> shift) & 0xffu; }

// Each channel from a to b (0xAABBGGRR).
u32 Mix(u32 a, u32 b, f32 t)
{
	u32 result = 0;
	for (u32 shift = 0; shift < 32; shift += 8)
	{
		const f32 value =
			f32(Channel(a, shift)) + (f32(Channel(b, shift)) - f32(Channel(a, shift))) * t;
		result |= u32(std::clamp(value, 0.0f, 255.0f) + 0.5f) << shift;
	}
	return result;
}

u32 Color(Vec3 rgb, f32 alpha) { return PackColor(rgb.x, rgb.y, rgb.z, alpha); }

// The engines' nozzles and the nose's sides, from a model's bounds.
struct Mounts
{
	Vec3 nozzles[2];
	Vec3 nose[2];
};

Mounts MountsOf(const assets::MeshBounds& bounds)
{
	const f32 halfWidth = 0.5f * (bounds.max.x - bounds.min.x);
	const f32 y = 0.5f * (bounds.min.y + bounds.max.y);
	Mounts mounts;
	mounts.nozzles[0] = {-0.42f * halfWidth, y, bounds.max.z};
	mounts.nozzles[1] = {0.42f * halfWidth, y, bounds.max.z};
	mounts.nose[0] = {-0.3f * halfWidth, y, 0.75f * bounds.min.z};
	mounts.nose[1] = {0.3f * halfWidth, y, 0.75f * bounds.min.z};
	return mounts;
}
} // namespace

void FlightEffects::Reset()
{
	particles.clear();
	smoke.clear();
	ships.clear();
}

f32 FlightEffects::Random()
{
	seed = seed * 1664525u + 1013904223u;
	return f32(seed >> 8) / f32(1u << 24);
}

FlightEffects::Looks* FlightEffects::Find(u32 id)
{
	for (Looks& looks : ships)
	{
		if (looks.id == id)
			return &looks;
	}
	return nullptr;
}

void FlightEffects::Emit(std::vector<Particle>& into, Vec3 at, Vec3 velocity, f32 life,
                         f32 startSize, f32 endSize, u32 startColor, u32 endColor)
{
	if (into.size() >= (&into == &smoke ? MAX_SMOKE : MAX_PARTICLES))
		return;
	into.push_back({at, velocity, 0.0f, life, startSize, endSize, startColor, endColor});
}

void FlightEffects::Begin(f32 frameSeconds)
{
	dt = frameSeconds;
	for (std::vector<Particle>* list : {&particles, &smoke})
	{
		for (Particle& particle : *list)
		{
			particle.age += dt;
			particle.position = particle.position + particle.velocity * dt;
			particle.velocity = particle.velocity * std::max(0.0f, 1.0f - 2.5f * dt);
		}
		list->erase(std::remove_if(list->begin(), list->end(),
		                           [](const Particle& p) { return p.age >= p.life; }),
		            list->end());
	}
	for (Looks& looks : ships)
	{
		looks.seen = false;
		for (std::vector<TrailPoint>& trail : looks.trails)
		{
			for (TrailPoint& point : trail)
				point.age += dt;
			trail.erase(std::remove_if(trail.begin(), trail.end(),
			                           [](const TrailPoint& p) { return p.age > TRAIL_SECONDS; }),
			            trail.end());
		}
		for (Hit& hit : looks.hits)
			hit.age += dt;
		looks.hits.erase(std::remove_if(looks.hits.begin(), looks.hits.end(),
		                                [](const Hit& h) { return h.age >= HIT_SECONDS; }),
		                 looks.hits.end());
	}
	// Ships gone (destroyed, or out of the snapshots) once their trails fade.
	ships.erase(std::remove_if(ships.begin(), ships.end(), [](const Looks& looks)
	                           { return looks.trails[0].empty() && looks.trails[1].empty(); }),
	            ships.end());
}

void FlightEffects::Ship(u32 id, const ShipLook& look, const Mat4& model, Vec3 velocity, f32 thrust,
                         f32 damage)
{
	Looks* looks = Find(id);
	if (!looks)
	{
		ships.push_back({});
		looks = &ships.back();
		looks->id = id;
	}
	looks->seen = true;
	looks->look = look;
	looks->center = Point(model, {});

	const Mounts mounts = MountsOf(look.bounds);
	const Vec3 back = Direction(model, {0.0f, 0.0f, 1.0f});
	const Vec3 side = Direction(model, {1.0f, 0.0f, 0.0f});
	const Vec3 up = Direction(model, {0.0f, 1.0f, 0.0f});
	const auto jitter = [&](f32 amount)
	{ return side * ((Random() - 0.5f) * amount) + up * ((Random() - 0.5f) * amount); };

	// Flame while thrusting: hotter, faster and longer the harder.
	if (thrust > 0.02f)
	{
		looks->owedFlame += FLAME_RATE * look.flameRate * thrust * dt;
		for (; looks->owedFlame >= 1.0f; looks->owedFlame -= 1.0f)
		{
			for (const Vec3& nozzle : mounts.nozzles)
			{
				const f32 speed = (10.0f + 8.0f * Random()) * (0.6f + 0.4f * thrust);
				Emit(particles, Point(model, nozzle) + jitter(0.1f),
				     velocity * 0.5f + back * speed + jitter(2.0f),
				     0.12f + 0.12f * Random() + 0.1f * thrust, 0.2f, 0.45f + 0.3f * thrust,
				     look.flameHot, look.flameCold);
			}
		}
	}
	else
		looks->owedFlame = 0.0f;
	// Puffs at the nose while reversing.
	if (thrust < -0.02f)
	{
		looks->owedPuffs += PUFF_RATE * look.flameRate * -thrust * dt;
		const u32 cool = PackColor(0.6f, 0.85f, 1.0f, 0.7f);
		const u32 gone = PackColor(0.3f, 0.5f, 1.0f, 0.0f);
		for (; looks->owedPuffs >= 1.0f; looks->owedPuffs -= 1.0f)
		{
			for (const Vec3& nose : mounts.nose)
			{
				Emit(particles, Point(model, nose) + jitter(0.1f),
				     velocity * 0.5f - back * (4.0f + 3.0f * Random()) + jitter(2.0f),
				     0.12f + 0.1f * Random(), 0.1f, 0.3f, cool, gone);
			}
		}
	}
	else
		looks->owedPuffs = 0.0f;
	// Smoke from a hull past half its damage; sparks from one about to break.
	if (damage > 0.5f)
	{
		const f32 worse = (damage - 0.5f) / 0.5f;
		looks->owedSmoke += SMOKE_RATE * worse * dt;
		for (; looks->owedSmoke >= 1.0f; looks->owedSmoke -= 1.0f)
		{
			Emit(smoke, looks->center + jitter(0.8f), velocity * 0.3f + jitter(1.5f),
			     0.8f + 0.6f * Random(), 0.3f, 1.1f + 0.4f * Random(),
			     PackColor(0.13f, 0.12f, 0.11f, 0.5f), PackColor(0.05f, 0.05f, 0.05f, 0.0f));
		}
		if (damage > 0.75f && Random() < 4.0f * dt)
			Sparks(looks->center + jitter(1.0f), 3);
	}
	else
		looks->owedSmoke = 0.0f;

	// The engines' hot cores, flickering while they burn.
	for (u32 i = 0; i < 2; ++i)
		looks->nozzles[i] = Point(model, mounts.nozzles[i]);
	looks->burn = std::max(0.0f, thrust);
	looks->flicker = 0.85f + 0.3f * Random();

	// Trails: a point behind each engine every frame, fading with age.
	const f32 strength =
		std::min(1.0f, std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z) / TRAIL_SPEED);
	for (u32 i = 0; i < 2; ++i)
	{
		std::vector<TrailPoint>& trail = looks->trails[i];
		if (trail.size() >= MAX_TRAIL_POINTS)
			trail.erase(trail.begin());
		trail.push_back({looks->nozzles[i], 0.0f, strength});
	}
}

void FlightEffects::Bump(u32 id, Vec3 direction, f32 strength)
{
	Looks* looks = Find(id);
	if (!looks || Length(direction) < 1e-4f)
		return;
	looks->hits.push_back(
		{Normalize(direction), 0.0f, std::clamp(0.5f + strength / 12.0f, 0.5f, 1.5f)});
}

void FlightEffects::Sparks(Vec3 at, u32 count)
{
	const u32 bright = PackColor(1.0f, 0.9f, 0.6f, 1.0f);
	const u32 gone = PackColor(1.0f, 0.45f, 0.1f, 0.0f);
	for (u32 i = 0; i < count; ++i)
	{
		const f32 angle = Random() * 2.0f * PI;
		const f32 speed = 6.0f + 9.0f * Random();
		Emit(particles, at,
		     Vec3{std::cos(angle) * speed, (Random() - 0.2f) * 4.0f, std::sin(angle) * speed},
		     0.15f + 0.15f * Random(), 0.14f, 0.03f, bright, gone);
	}
}

void FlightEffects::Explosion(Vec3 at, f32 size)
{
	const auto outward = [this]()
	{
		const Vec3 way = {2.0f * Random() - 1.0f, 0.4f * (2.0f * Random() - 1.0f),
		                  2.0f * Random() - 1.0f};
		return Length(way) > 1e-3f ? Normalize(way) : Vec3{1.0f, 0.0f, 0.0f};
	};
	// A flash; a fireball growing and cooling; fast sparks; then smoke.
	Emit(particles, at, {}, 0.22f, 3.0f * size, 0.6f * size, PackColor(1.0f, 0.95f, 0.85f, 1.0f),
	     PackColor(1.0f, 0.6f, 0.2f, 0.0f));
	for (u32 i = 0; i < 28; ++i)
	{
		const Vec3 way = outward();
		Emit(particles, at + way * 0.3f, way * ((3.0f + 9.0f * Random()) * size / 1.5f),
		     0.45f + 0.5f * Random(), 0.5f * size, (1.1f + 0.6f * Random()) * size,
		     PackColor(1.0f, 0.7f, 0.3f, 0.9f), PackColor(0.5f, 0.08f, 0.02f, 0.0f));
	}
	for (u32 i = 0; i < 20; ++i)
	{
		Emit(particles, at, outward() * (14.0f + 16.0f * Random()), 0.25f + 0.3f * Random(), 0.16f,
		     0.03f, PackColor(1.0f, 0.92f, 0.6f, 1.0f), PackColor(1.0f, 0.4f, 0.1f, 0.0f));
	}
	for (u32 i = 0; i < 10; ++i)
	{
		Emit(smoke, at + outward() * (0.5f * size), outward() * (2.0f + 3.0f * Random()),
		     1.2f + 0.8f * Random(), 0.6f * size, 1.8f * size, PackColor(0.1f, 0.09f, 0.08f, 0.6f),
		     PackColor(0.04f, 0.04f, 0.04f, 0.0f));
	}
}

void FlightEffects::WarpIn(Vec3 at, Vec3 color)
{
	const Vec3 white = {1.0f, 1.0f, 1.0f};
	Emit(particles, at, {}, 0.35f, 0.5f, 4.5f, Color(white * 0.5f + color * 0.5f, 0.9f),
	     Color(color, 0.0f));
	for (u32 i = 0; i < 24; ++i)
	{
		const f32 angle = f32(i) * (2.0f * PI / 24.0f);
		const Vec3 way = {std::cos(angle), 0.0f, std::sin(angle)};
		Emit(particles, at + way * 0.5f, way * 14.0f, 0.35f, 0.25f, 0.08f, Color(color, 0.9f),
		     Color(color, 0.0f));
	}
}

void FlightEffects::Draw(rhi::CommandList& commands, const Resources& resources,
                         const render::CameraAxes& camera)
{
	// Trails: soft lines, narrowing and fading behind the engines.
	vertices.clear();
	for (const Looks& looks : ships)
	{
		for (const std::vector<TrailPoint>& trail : looks.trails)
		{
			ribbon.clear();
			for (auto point = trail.rbegin(); point != trail.rend(); ++point)
			{
				const f32 left = 1.0f - point->age / TRAIL_SECONDS;
				ribbon.push_back({point->position, 0.04f + 0.22f * left,
				                  Color(looks.look.trail, 0.55f * left * left * point->strength)});
			}
			render::AddRibbon(vertices, ribbon.data(), u32(ribbon.size()), camera);
		}
	}
	render::DrawEffect(commands, vertices.data(), u32(vertices.size()), rhi::BlendMode::Additive,
	                   render::EffectShape::Strip);

	// Smoke darkens what is behind it; glows add to it.
	const auto billboards = [&](const std::vector<Particle>& list)
	{
		for (const Particle& particle : list)
		{
			const f32 t = particle.age / particle.life;
			render::AddBillboard(vertices, particle.position,
			                     particle.startSize + (particle.endSize - particle.startSize) * t,
			                     Mix(particle.startColor, particle.endColor, t), camera);
		}
	};
	vertices.clear();
	billboards(smoke);
	render::DrawEffect(commands, vertices.data(), u32(vertices.size()), rhi::BlendMode::Alpha,
	                   render::EffectShape::Disc);

	// Flame, puffs, sparks and blasts: soft spots, growing and cooling with
	// age; over them, the cores of burning engines.
	vertices.clear();
	billboards(particles);
	for (const Looks& looks : ships)
	{
		if (!looks.seen || looks.burn <= 0.02f)
			continue;
		const u32 core = Mix(looks.look.core & 0x00ffffffu, looks.look.core, looks.burn);
		for (const Vec3& nozzle : looks.nozzles)
			render::AddBillboard(vertices, nozzle, (0.3f + 0.35f * looks.burn) * looks.flicker,
			                     core, camera);
	}
	render::DrawEffect(commands, vertices.data(), u32(vertices.size()), rhi::BlendMode::Additive,
	                   render::EffectShape::Disc);

	// Shields: lit toward each recent hit, their outline faint meanwhile.
	if (resources.shieldIndices.empty())
		return;
	vertices.clear();
	for (const Looks& looks : ships)
	{
		if (!looks.seen || looks.hits.empty())
			continue;
		f32 outline = 0.0f;
		for (const Hit& hit : looks.hits)
			outline = std::max(outline, 1.0f - hit.age / HIT_SECONDS);
		const f32 radius = looks.look.shieldRadius;
		for (u32 index : resources.shieldIndices)
		{
			const Vec3 normal = resources.shieldPoints[index];
			const Vec3 position = looks.center + normal * radius;
			f32 glow = 0.0f;
			for (const Hit& hit : looks.hits)
			{
				const f32 toward = (Dot(normal, hit.direction) - 0.25f) / 0.75f;
				if (toward > 0.0f)
				{
					const f32 fade = 1.0f - hit.age / HIT_SECONDS;
					glow += toward * toward * fade * std::sqrt(fade) * hit.strength;
				}
			}
			const f32 facing = std::fabs(Dot(normal, Normalize(camera.position - position)));
			const f32 rim = (1.0f - facing) * (1.0f - facing) * (1.0f - facing) * 0.35f * outline;
			vertices.push_back(
				{position, 0.0f, 0.0f, Color(looks.look.shield, std::min(1.0f, glow + rim))});
		}
	}
	render::DrawEffect(commands, vertices.data(), u32(vertices.size()), rhi::BlendMode::Additive,
	                   render::EffectShape::Flat);
}
} // namespace sn
