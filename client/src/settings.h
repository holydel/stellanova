#pragma once

#include <ph/os/window.h>

// The player's settings, kept as settings.txt (key = value lines) among the
// app's saved files: its data folder, or the browser's storage on the web.
namespace sn
{
// How the interface lays out (docs/adr/0018-one-interface-every-device.md):
// Desktop for big screens and the Deck (mouse, keys, pads), Mobile for phones
// (touch first: tabs at the bottom, stacked panels, bigger targets). Auto
// picks by the device.
enum class Style : ph::u8
{
	Auto,
	Desktop,
	Mobile,
};

constexpr ph::f32 MIN_INTERFACE_SIZE = 0.8f;
constexpr ph::f32 MAX_INTERFACE_SIZE = 1.5f;

struct Settings
{
	ph::f32 music = 0.7f;   // 0 to 1
	ph::f32 effects = 1.0f; // effects and the interface
	bool fullscreen = false;
	Style style = Style::Auto;
	// The interface's size over the screen's own, in tenths from 0.8 to 1.5.
	ph::f32 interfaceSize = 1.0f;

	void Load();
	void Save() const;
	// To the audio buses and the window.
	void Apply(ph::os::WindowId window) const;
	// Auto made concrete: Mobile on phones (Android; the web on a screen whose
	// short side is under 600 units; a desktop pretending to be one, with
	// touch: ph/os/simulation.h), Desktop elsewhere.
	Style ResolvedStyle(ph::os::WindowId window) const;
};

const char* StyleName(Style style);
} // namespace sn
