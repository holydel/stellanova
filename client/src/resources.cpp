#include "resources.h"

#include <ph/core/log.h>
#include <ph/render/canvas.h>
#include <ph/render/sky.h>

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
} // namespace

bool Resources::Load(rhi::Format sceneFormat)
{
	if (!render::InitCanvas() || !render::InitMaterials() ||
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
	const f32 stone[4] = {0.55f, 0.5f, 0.48f, 1.0f}; // the meteors' salmon, greyer
	if (!render::CreateMaterial(render::LitMaterialType(), rockMaterial) ||
	    !render::SetMaterialValue(rockMaterial, "baseColor", stone, sizeof(stone)) ||
	    !render::LoadPackMesh(pack, "ship", ship) ||
	    !render::LoadPackMesh(pack, "rock_a", rocks[0]) ||
	    !render::LoadPackMesh(pack, "rock_b", rocks[1]) ||
	    !render::LoadPackMesh(pack, "rock_c", rocks[2]) ||
	    !render::CreateMaterial(render::LitMaterialType(), material) ||
	    !render::SetMaterialValue(material, "baseColor", white, sizeof(white)))
		return false;

	move = LoadSound(pack, "ui_move", false);
	confirm = LoadSound(pack, "ui_confirm", false);
	back = LoadSound(pack, "ui_back", false);
	ambience = LoadSound(pack, "ambience", false);
	thruster = LoadSound(pack, "thruster", false);
	return true;
}

void Resources::Destroy()
{
	render::DestroyMaterial(material);
	render::DestroyMaterial(rockMaterial);
	render::DestroyMesh(ship);
	for (render::Mesh& rock : rocks)
		render::DestroyMesh(rock);
	render::DestroyTextFont(sansFont);
	render::DestroyTextFont(boldFont);
	render::ShutdownText();
	textures.Destroy();
	render::ShutdownSky();
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
} // namespace sn
