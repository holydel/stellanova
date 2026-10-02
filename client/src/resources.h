#pragma once

#include "string_table.h"

#include <ph/assets/pack.h>
#include <ph/audio/audio.h>
#include <ph/core/math.h>
#include <ph/render/material.h>
#include <ph/render/mesh.h>
#include <ph/render/text.h>
#include <ph/render/textures.h>

#include <vector>

// What the screens draw and play, from stellanova.pak (content/initial.json):
// fonts, the sky, models baked from glTF (Kenney's Space Kit for now), sounds
// and the strings.
namespace sn
{
constexpr ph::u32 ROCK_MESHES = 3;

struct Resources
{
	ph::assets::Pack pack;
	ph::render::PackTextures textures;
	ph::rhi::Texture sky;
	ph::render::TextFont* sansFont = nullptr;
	ph::render::TextFont* boldFont = nullptr;
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

	// After the pack is read and the device is ready: the scene's formats.
	bool Load(ph::rhi::Format sceneFormat);
	void Destroy();

	// The ship model, placed by `world`: its nose along -Z, its top +Y.
	void DrawShip(ph::rhi::CommandList& commands, const ph::Mat4& world);
	void Play(ph::audio::Sound sound) const; // the interface's
	// `pan`: -1 left to 1 right.
	void PlayEffect(ph::audio::Sound sound, ph::f32 volume = 1.0f, ph::f32 pan = 0.0f) const;
};
} // namespace sn
