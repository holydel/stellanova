#include "resources.h"

#include <sn/sim/catalog.h>

#include <ph/core/log.h>
#include <ph/render/canvas.h>
#include <ph/render/effects.h>
#include <ph/render/primitives.h>
#include <ph/render/shapes.h>
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

// Meshy's metal maps make painted walls half metal, pale and silvery: much
// less, times this.
constexpr f32 MODEL_METAL = 0.3f;
// The colony's light (scripts/colony_light.py) is as the regolith sends it;
// the concept's shade is much brighter, the buildings' fronts readable.
constexpr f32 COLONY_LIGHT = 8.0f;
constexpr f32 CRATER_TINT[4] = {0.42f, 0.42f, 0.44f, 1.0f};

// Which way each generated ship's nose points in its file: turned by this
// about y, it points to -z, as the Kenney ships' (FlightEffects' mounts).
constexpr f32 LANCER_TURN = -1.5707963f; // its nose is at -x
constexpr f32 RAIDER_TURN = -1.5707963f;

// A shield's radius: it covers the model whole.
f32 ShieldRadius(const assets::MeshBounds& bounds)
{
	return 1.1f *
	       std::max({std::fabs(bounds.min.x), std::fabs(bounds.max.x), std::fabs(bounds.min.y),
	                 std::fabs(bounds.max.y), std::fabs(bounds.min.z), std::fabs(bounds.max.z)});
}

// The art pack's model `id`, its metal tamed. False when the pack lacks it.
bool LoadModel(Resources& resources, const char* id, render::Model& model)
{
	if (!resources.art.Find(id) ||
	    !render::LoadPackModel(resources.art, resources.artTextures, id, model))
		return false;
	for (usize m = 0; m < model.materials.size(); ++m)
	{
		const assets::ModelMaterial& source = model.sources[m];
		const f32 factors[4] = {MODEL_METAL * source.metallic, source.roughness, source.normalScale,
		                        source.occlusionStrength};
		render::SetMaterialValue(model.materials[m], "factors", factors, sizeof(factors));
	}
	PH_LOG_INFO("resources: the model %s, %u triangles", id, model.mesh.indexCount / 3);
	return true;
}

// A generated ship in place of a Kenney one: turned nose to -z, as long as
// the Kenney ship and centered on it; the flight's looks (engines, nose,
// shield) then follow its bounds.
void LoadShip(Resources& resources, const char* id, f32 turn, assets::MeshBounds& bounds,
              f32& shieldRadius, Resources::ShipModel& ship)
{
	if (!LoadModel(resources, id, ship.model))
		return;
	const Mat4 turned = RotationY(turn);
	Vec3 low = {1e30f, 1e30f, 1e30f};
	Vec3 high = {-1e30f, -1e30f, -1e30f};
	for (u32 corner = 0; corner < 8; ++corner)
	{
		const assets::MeshBounds& from = ship.model.bounds;
		const Vec4 p = turned * Vec4{corner & 1 ? from.max.x : from.min.x,
		                             corner & 2 ? from.max.y : from.min.y,
		                             corner & 4 ? from.max.z : from.min.z, 1.0f};
		low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
		high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
	}
	const f32 scale = (bounds.max.z - bounds.min.z) / std::max(high.z - low.z, 1e-4f);
	const Vec3 middle = (low + high) * 0.5f;
	ship.fit = Scale({scale, scale, scale}) * Translation(middle * -1.0f) * turned;
	bounds.min = (low - middle) * scale;
	bounds.max = (high - middle) * scale;
	shieldRadius = ShieldRadius(bounds);
	ship.loaded = true;
}

// The art pack's light, and the buildings and ships it has models of (named
// by the catalog's ids); Kenney's stand in for the rest.
void LoadModels(Resources& resources)
{
	if (!resources.art.Find("colony_light") ||
	    !render::LoadPackEnvironment(resources.art, resources.artTextures, "colony_light", "brdf",
	                                 resources.light))
		return;
	resources.light.sun = true;
	resources.light.intensity = COLONY_LIGHT;
	for (u32 b = 0; b < COLONY_BUILDINGS; ++b)
		resources.colonyModeled[b] =
			LoadModel(resources, sim::BuildingId(sim::Building(b)), resources.colonyModels[b]);
	// The crater's regolith, as dark as the scene's ground around it.
	resources.craterModeled = LoadModel(resources, "crater", resources.crater);
	for (render::Material& material : resources.crater.materials)
		render::SetMaterialValue(material, "baseColor", CRATER_TINT, sizeof(CRATER_TINT));
	LoadShip(resources, "lancer", LANCER_TURN, resources.shipBounds, resources.shieldRadius,
	         resources.shipModel);
	LoadShip(resources, "raider", RAIDER_TURN, resources.enemyBounds, resources.enemyShieldRadius,
	         resources.enemyModel);
}
} // namespace

bool Resources::Load(rhi::Format sceneFormat)
{
	if (!render::InitCanvas() || !render::InitShapes() || !render::InitMaterials() ||
	    !render::InitPbr() || !render::InitEffects() ||
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
	// The icons (scripts/icons.py): characters of the Private Use Area,
	// after each font, so that an icon in any text draws like its letters.
	if (pack.Find("icons"))
	{
		iconsFont = render::LoadPackFont(pack, textures, "icons");
		sans.Add(iconsFont);
		bold.Add(iconsFont);
	}
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
	const char* const colonyNames[COLONY_BUILDINGS] = {
		"colony_mine", "colony_extractor", "colony_fab", "colony_depot", "colony_command"};
	const char* const propNames[COLONY_PROPS] = {"colony_dish", "colony_crater", "colony_pad"};
	for (u32 i = 0; i < COLONY_BUILDINGS; ++i)
	{
		if (!render::LoadPackMesh(pack, colonyNames[i], colony[i], &colonyBounds[i]))
			return false;
	}
	for (u32 i = 0; i < COLONY_PROPS; ++i)
	{
		if (!render::LoadPackMesh(pack, propNames[i], colonyProps[i], &colonyPropBounds[i]))
			return false;
	}
	const f32 regolith[4] = {0.2f, 0.205f, 0.22f, 1.0f};
	const f32 concrete[4] = {0.12f, 0.125f, 0.14f, 1.0f};
	const f32 asphalt[4] = {0.07f, 0.075f, 0.085f, 1.0f};
	const f32 boulder[4] = {0.17f, 0.17f, 0.18f, 1.0f};
	const f32 giant[4] = {0.3f, 0.4f, 0.55f, 1.0f};
	const f32 none[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	const f32 picked[4] = {0.05f, 0.16f, 0.24f, 1.0f};
	if (!render::CreateMesh(render::MakePlane(8000.0f, 1), ground) ||
	    !render::CreateMesh(render::MakeCylinder(1.0f, 1.0f, 8), pad) ||
	    !render::CreateMaterial(render::LitMaterialType(), padMaterial) ||
	    !render::SetMaterialValue(padMaterial, "baseColor", concrete, sizeof(concrete)) ||
	    !render::CreateMaterial(render::LitMaterialType(), roadMaterial) ||
	    !render::SetMaterialValue(roadMaterial, "baseColor", asphalt, sizeof(asphalt)) ||
	    !render::CreateMaterial(render::LitMaterialType(), boulderMaterial) ||
	    !render::SetMaterialValue(boulderMaterial, "baseColor", boulder, sizeof(boulder)) ||
	    !render::CreateMesh(render::MakeUvSphere(1.0f, 48, 24), planet) ||
	    !render::CreateMaterial(render::LitMaterialType(), groundMaterial) ||
	    !render::SetMaterialValue(groundMaterial, "baseColor", regolith, sizeof(regolith)) ||
	    !render::CreateMaterial(render::LitMaterialType(), planetMaterial) ||
	    !render::SetMaterialValue(planetMaterial, "baseColor", giant, sizeof(giant)) ||
	    !render::CreateMaterial(render::LitMaterialType(), litMaterial) ||
	    !render::SetMaterialValue(litMaterial, "baseColor", white, sizeof(white)) ||
	    !render::SetMaterialValue(litMaterial, "emissive", picked, sizeof(picked)) ||
	    !render::CreateMaterial(render::LitMaterialType(), busyMaterial) ||
	    !render::SetMaterialValue(busyMaterial, "baseColor", white, sizeof(white)) ||
	    !render::SetMaterialValue(busyMaterial, "emissive", none, sizeof(none)))
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

bool Resources::LoadArt()
{
	if (!render::CreatePackTextures(art, artTextures))
		return false;
	LoadModels(*this);
	const assets::PackEntry* entry = art.Find("splash");
	if (!entry)
		return false;
	splashTexture = artTextures.Find("splash");
	splash = render::CreateSpriteTexture(splashTexture);
	splashWidth = entry->width;
	splashHeight = entry->height;
	return HasSplash();
}

void Resources::Destroy()
{
	render::DestroySpriteTexture(splash);
	for (render::Model& model : colonyModels)
		render::DestroyModel(model);
	render::DestroyModel(crater);
	render::DestroyModel(shipModel.model);
	render::DestroyModel(enemyModel.model);
	render::DestroyEnvironment(light);
	artTextures.Destroy();
	render::DestroyMaterial(material);
	render::DestroyMaterial(enemyMaterial);
	render::DestroyMaterial(rockMaterial);
	render::DestroyMaterial(sceneryMaterial);
	render::DestroyMaterial(boltMaterial);
	render::DestroyMaterial(enemyBoltMaterial);
	render::DestroyMaterial(debrisMaterial);
	render::DestroyMaterial(groundMaterial);
	render::DestroyMaterial(boulderMaterial);
	render::DestroyMaterial(padMaterial);
	render::DestroyMaterial(roadMaterial);
	render::DestroyMesh(pad);
	render::DestroyMaterial(planetMaterial);
	render::DestroyMaterial(litMaterial);
	render::DestroyMaterial(busyMaterial);
	for (render::Mesh& mesh : colony)
		render::DestroyMesh(mesh);
	for (render::Mesh& mesh : colonyProps)
		render::DestroyMesh(mesh);
	render::DestroyMesh(ground);
	render::DestroyMesh(planet);
	render::DestroyMesh(bolt);
	render::DestroyMesh(ship);
	render::DestroyMesh(enemy);
	for (render::Mesh& rock : rocks)
		render::DestroyMesh(rock);
	render::DestroyTextFont(sansFont);
	render::DestroyTextFont(boldFont);
	render::DestroyTextFont(iconsFont);
	render::ShutdownText();
	textures.Destroy();
	render::ShutdownShapes();
	render::ShutdownSky();
	render::ShutdownEffects();
	render::ShutdownMeshes();
	render::ShutdownPbr();
	render::ShutdownMaterials();
	render::ShutdownCanvas();
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
