#include "resources.h"

#include <ph/core/log.h>
#include <ph/render/canvas.h>
#include <ph/render/effects.h>
#include <ph/render/primitives.h>
#include <ph/render/sky.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string_view>

namespace sn
{
namespace
{
using namespace ph;

audio::Sound LoadSound(const assets::Pack& pack, const char* name, bool stream)
{
	const assets::PackEntry* entry = pack.Find(name);
	if (!entry)
	{
		PH_LOG_WARN("resources: the pack has no sound %s", name);
		return {};
	}
	return audio::LoadSound({entry->data, usize(entry->size), name, stream});
}

// A shield's radius: it covers the model whole.
f32 ShieldRadius(const assets::MeshBounds& bounds)
{
	return 1.1f *
	       std::max({std::fabs(bounds.min.x), std::fabs(bounds.max.x), std::fabs(bounds.min.y),
	                 std::fabs(bounds.max.y), std::fabs(bounds.min.z), std::fabs(bounds.max.z)});
}
} // namespace

bool Resources::Load(rhi::Format sceneFormat)
{
	if (!render::InitCanvas() || !render::InitMaterials() || !render::InitEffects() ||
	    !render::InitMeshes(sceneFormat, rhi::Format::Depth32Float) ||
	    !render::InitSky(sceneFormat, rhi::Format::Depth32Float) ||
	    !render::CreatePackTextures(pack, textures))
		return false;
	sky = textures.Find("sky");
	sansFont = render::LoadPackFont(pack, textures, "sans");
	boldFont = render::LoadPackFont(pack, textures, "sans_bold");
	if (!sansFont || !boldFont)
		return false;
	sans.Add(sansFont);
	bold.Add(boldFont);
	if (const assets::PackEntry* entry = pack.Find("strings.en"))
		strings.Load({reinterpret_cast<const char*>(entry->data), usize(entry->size)});

	const f32 white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	const f32 stone[4] = {0.55f, 0.5f, 0.48f, 1.0f};       // the meteors' salmon, greyer
	const f32 distant[4] = {0.025f, 0.024f, 0.028f, 1.0f}; // the sun is bright: much darker
	const f32 black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	const f32 glow[4] = {1.0f, 0.72f, 0.3f, 1.0f};
	const f32 hostile[4] = {1.0f, 0.32f, 0.3f, 1.0f};     // Kenney's orange and white, red
	const f32 hostileGlow[4] = {1.0f, 0.2f, 0.45f, 1.0f}; // the bots' shots: pink-red
	const f32 metal[4] = {0.16f, 0.15f, 0.15f, 1.0f};
	const char* const rockNames[ROCK_MESHES] = {"rock_a", "rock_b", "rock_c"};
	if (!render::CreateMaterial(render::LitMaterialType(), rockMaterial) ||
	    !render::SetMaterialValue(rockMaterial, "baseColor", stone, sizeof(stone)) ||
	    !render::CreateMaterial(render::LitMaterialType(), sceneryMaterial) ||
	    !render::SetMaterialValue(sceneryMaterial, "baseColor", distant, sizeof(distant)) ||
	    !render::CreateMaterial(render::LitMaterialType(), boltMaterial) ||
	    !render::SetMaterialValue(boltMaterial, "baseColor", black, sizeof(black)) ||
	    !render::SetMaterialValue(boltMaterial, "emissive", glow, sizeof(glow)) ||
	    !render::CreateMaterial(render::LitMaterialType(), enemyBoltMaterial) ||
	    !render::SetMaterialValue(enemyBoltMaterial, "baseColor", black, sizeof(black)) ||
	    !render::SetMaterialValue(enemyBoltMaterial, "emissive", hostileGlow,
	                              sizeof(hostileGlow)) ||
	    !render::CreateMaterial(render::LitMaterialType(), debrisMaterial) ||
	    !render::SetMaterialValue(debrisMaterial, "baseColor", metal, sizeof(metal)) ||
	    !render::CreateMesh(render::MakeCube(1.0f), bolt) ||
	    !render::LoadPackMesh(pack, "ship", ship, &shipBounds) ||
	    !render::LoadPackMesh(pack, "enemy", enemy, &enemyBounds) ||
	    !render::CreateMaterial(render::LitMaterialType(), material) ||
	    !render::SetMaterialValue(material, "baseColor", white, sizeof(white)) ||
	    !render::CreateMaterial(render::LitMaterialType(), enemyMaterial) ||
	    !render::SetMaterialValue(enemyMaterial, "baseColor", hostile, sizeof(hostile)))
		return false;
	for (u32 i = 0; i < ROCK_MESHES; ++i)
	{
		assets::MeshBounds bounds;
		if (!render::LoadPackMesh(pack, rockNames[i], rocks[i], &bounds))
			return false;
		rockExtents[i] = std::max({std::fabs(bounds.min.x), std::fabs(bounds.max.x),
		                           std::fabs(bounds.min.z), std::fabs(bounds.max.z), 0.01f});
	}

	const render::MeshData shield = render::MakeGeosphere(1.0f, 3);
	shieldPoints = shield.positions;
	shieldIndices = shield.indices;
	shieldRadius = ShieldRadius(shipBounds);
	enemyShieldRadius = ShieldRadius(enemyBounds);

	move = LoadSound(pack, "ui_move", false);
	confirm = LoadSound(pack, "ui_confirm", false);
	back = LoadSound(pack, "ui_back", false);
	ambience = LoadSound(pack, "ambience", false);
	thruster = LoadSound(pack, "thruster", false);
	fire = LoadSound(pack, "fire", false);
	hit = LoadSound(pack, "hit", false);
	explode = LoadSound(pack, "explode", false);
	bump = LoadSound(pack, "bump", false);
	enemyFire = LoadSound(pack, "enemy_fire", false);
	shieldHit = LoadSound(pack, "shield_hit", false);
	hullHit = LoadSound(pack, "hull_hit", false);
	blast = LoadSound(pack, "blast", false);
	warp = LoadSound(pack, "warp", false);
	return true;
}

void Resources::Destroy()
{
	render::DestroyMaterial(material);
	render::DestroyMaterial(enemyMaterial);
	render::DestroyMaterial(rockMaterial);
	render::DestroyMaterial(sceneryMaterial);
	render::DestroyMaterial(boltMaterial);
	render::DestroyMaterial(enemyBoltMaterial);
	render::DestroyMaterial(debrisMaterial);
	render::DestroyMesh(bolt);
	render::DestroyMesh(ship);
	render::DestroyMesh(enemy);
	for (render::Mesh& rock : rocks)
		render::DestroyMesh(rock);
	render::DestroyTextFont(sansFont);
	render::DestroyTextFont(boldFont);
	render::ShutdownText();
	textures.Destroy();
	render::ShutdownSky();
	render::ShutdownEffects();
	render::ShutdownMeshes();
	render::ShutdownMaterials();
	render::ShutdownCanvas();
}

void Resources::DrawShip(rhi::CommandList& commands, const Mat4& world)
{
	// Kenney's craft point their noses along -Z already.
	render::DrawMesh(commands, ship, material, world);
}

void Resources::Play(audio::Sound sound) const
{
	audio::PlayDesc desc;
	desc.bus = audio::Bus::Interface;
	audio::Play(sound, desc);
}

void Resources::PlayEffect(audio::Sound sound, f32 volume, f32 pan) const
{
	audio::PlayDesc desc;
	desc.bus = audio::Bus::Effects;
	desc.volume = volume;
	desc.pan = pan;
	audio::Play(sound, desc);
}
} // namespace sn
