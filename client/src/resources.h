#pragma once

#include "string_table.h"

#include <ph/assets/pack.h>
#include <ph/audio/audio.h>
#include <ph/core/math.h>
#include <ph/render/canvas.h>
#include <ph/render/material.h>
#include <ph/render/mesh.h>
#include <ph/render/pbr.h>
#include <ph/render/text.h>
#include <ph/render/textures.h>

#include <vector>

// What the screens draw and play, from stellanova.pak (content/initial.json):
// fonts, the sky, models baked from glTF (Kenney's Space Kit for now), sounds
// and the strings.
namespace sn
{
constexpr ph::u32 ROCK_MESHES = 3;
constexpr ph::u32 COLONY_BUILDINGS = 5; // sim::BUILDINGS: mine, extractor, fab, depot, command
// The colony's other pieces: a dish, a crater, a landing pad.
constexpr ph::u32 COLONY_PROPS = 3;

struct Resources
{
	ph::assets::Pack pack;
	ph::render::PackTextures textures;
	ph::rhi::Texture sky;
	ph::render::TextFont* sansFont = nullptr;
	ph::render::TextFont* boldFont = nullptr;
	ph::render::TextFont* iconsFont = nullptr; // a fallback of both: icon_codes.h
	ph::render::TextFonts sans;
	ph::render::TextFonts bold;
	StringTable strings;

	ph::render::Mesh ship;
	ph::assets::MeshBounds shipBounds; // where its engines and nose are
	ph::render::Mesh enemy;            // the bots' fighters
	ph::assets::MeshBounds enemyBounds;
	ph::render::Mesh rocks[ROCK_MESHES];
	// Each rock model's half width (its largest of x and z), to scale it to
	// a rock's circle in the sim.
	ph::f32 rockExtents[ROCK_MESHES] = {};
	ph::render::Mesh bolt; // a cube: shots, and pieces of wrecks
	// The colony's scene (ColonyView): a model a building, Kenney's for now
	// (until the generated ones), its props, the ground and the gas giant.
	ph::render::Mesh colony[COLONY_BUILDINGS];
	ph::assets::MeshBounds colonyBounds[COLONY_BUILDINGS];
	ph::render::Mesh colonyProps[COLONY_PROPS];
	ph::assets::MeshBounds colonyPropBounds[COLONY_PROPS];
	ph::render::Mesh ground; // a plane
	ph::render::Mesh planet; // a sphere
	ph::render::Mesh pad;    // an octagonal prism: a building's pad
	// The shield around a ship: a geosphere's unit points and its triangles,
	// lit by the CPU each frame (FlightEffects); its radius covers the ship.
	std::vector<ph::Vec3> shieldPoints;
	std::vector<ph::u32> shieldIndices;
	ph::f32 shieldRadius = 2.0f;
	ph::f32 enemyShieldRadius = 2.0f;
	// White: the models' colors are in their vertices; the others tint them.
	ph::render::Material material;
	ph::render::Material enemyMaterial; // red: the bots'
	ph::render::Material rockMaterial;
	ph::render::Material sceneryMaterial;   // rocks far below the plane: darker
	ph::render::Material boltMaterial;      // glowing, unlit: our shots
	ph::render::Material enemyBoltMaterial; // the bots'
	ph::render::Material debrisMaterial;    // pieces of wrecks
	ph::render::Material groundMaterial;    // the colony's moon
	ph::render::Material boulderMaterial;   // its rocks
	ph::render::Material padMaterial;       // the buildings' pads
	ph::render::Material roadMaterial;      // the roads between them
	ph::render::Material planetMaterial;    // the gas giant in its sky
	ph::render::Material litMaterial;       // a building under the pointer, or picked
	ph::render::Material busyMaterial;      // a building being upgraded: glowing gold

	ph::audio::Sound move;
	ph::audio::Sound confirm;
	ph::audio::Sound back;
	ph::audio::Sound ambience;
	ph::audio::Sound thruster;
	ph::audio::Sound fire;
	ph::audio::Sound hit;
	ph::audio::Sound explode;
	ph::audio::Sound bump;
	ph::audio::Sound enemyFire;
	ph::audio::Sound shieldHit;
	ph::audio::Sound hullHit;
	ph::audio::Sound blast; // a ship breaking apart
	ph::audio::Sound warp;  // ships arriving

	// The menu's splash and the colony's generated buildings, from
	// stellanova-art.pak (content/art.json), in builds that have it: read
	// after the first frame.
	ph::assets::Pack art;
	ph::render::PackTextures artTextures;
	ph::render::SpriteTexture splash;
	ph::rhi::Texture splashTexture; // the same, for shapes (the hub's background)
	ph::u32 splashWidth = 0;
	ph::u32 splashHeight = 0;
	// PBR models named by the catalog's ids, each drawn in place of Kenney's
	// where the pack has it, in the colony's light (scripts/colony_light.py).
	ph::render::Model colonyModels[COLONY_BUILDINGS];
	bool colonyModeled[COLONY_BUILDINGS] = {};
	// Our ships' and the bots' (the Lancer, the Raider): `fit` takes the
	// model into the Kenney ship's frame, whose bounds become its own.
	struct ShipModel
	{
		ph::render::Model model;
		ph::Mat4 fit = ph::Mat4::Identity();
		bool loaded = false;
	};
	ShipModel shipModel;
	ShipModel enemyModel;
	ph::render::Model crater; // the colony's craters
	bool craterModeled = false;
	ph::render::Environment light;

	// After the pack is read and the device is ready: the scene's formats.
	bool Load(ph::rhi::Format sceneFormat);
	// After Load, once `art` is read: the splash and the buildings' models.
	// False when it holds no splash.
	bool LoadArt();
	bool HasSplash() const { return bool(splash.group); }
	void Destroy();

	void Play(ph::audio::Sound sound) const; // the interface's
	// `pan`: -1 left to 1 right.
	void PlayEffect(ph::audio::Sound sound, ph::f32 volume = 1.0f, ph::f32 pan = 0.0f) const;
};
} // namespace sn
