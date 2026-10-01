#pragma once

#include <ph/os/window.h>

// The player's settings, kept in settings.txt in the app's data folder
// (key = value lines).
namespace sn
{
struct Settings
{
	ph::f32 music = 0.7f;   // 0 to 1
	ph::f32 effects = 1.0f; // effects and the interface
	bool fullscreen = false;

	void Load();
	void Save() const;
	// To the audio buses and the window.
	void Apply(ph::os::WindowId window) const;
};
} // namespace sn
