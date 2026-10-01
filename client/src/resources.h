#pragma once

#include "string_table.h"

#include <ph/assets/pack.h>
#include <ph/audio/audio.h>
#include <ph/render/material.h>
#include <ph/render/mesh.h>
#include <ph/render/text.h>
#include <ph/render/textures.h>

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
	ph::render::Mesh rocks[ROCK_MESHES];
	// White: the models' colors are in their vertices; the rocks' tints them.
	ph::render::Material material;
	ph::render::Material rockMaterial;

	ph::audio::Sound move;
	ph::audio::Sound confirm;
	ph::audio::Sound back;
	ph::audio::Sound ambience;
	ph::audio::Sound thruster;

	// After the pack is read and the device is ready: the scene's formats.
	bool Load(ph::rhi::Format sceneFormat);
	void Destroy();

	// The ship model, placed by `world`: its nose along -Z, its top +Y.
	void DrawShip(ph::rhi::CommandList& commands, const ph::Mat4& world);
	void Play(ph::audio::Sound sound) const;
};
} // namespace sn
