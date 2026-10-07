#include "settings.h"

#include "key_values.h"

#include <ph/audio/audio.h>
#include <ph/core/log.h>
#include <ph/os/app.h>
#include <ph/os/simulation.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace sn
{
namespace
{
constexpr const char* FILE_NAME = "settings.txt";

ph::f32 Volume(const std::string& text)
{
	return std::clamp(std::strtof(text.c_str(), nullptr), 0.0f, 1.0f);
}

constexpr const char* STYLE_NAMES[] = {"auto", "desktop", "mobile"};
} // namespace

const char* StyleName(Style style) { return STYLE_NAMES[ph::u32(style)]; }

void Settings::Load()
{
	std::string text;
	if (!ph::os::ReadSavedFile(FILE_NAME, text))
		return; // the first run: the defaults
	for (const KeyValue& value : ParseKeyValues(text))
	{
		if (value.key == "music")
			music = Volume(value.value);
		else if (value.key == "effects")
			effects = Volume(value.value);
		else if (value.key == "fullscreen")
			fullscreen = value.value == "1";
		else if (value.key == "style")
		{
			for (ph::u32 i = 0; i < std::size(STYLE_NAMES); ++i)
			{
				if (value.value == STYLE_NAMES[i])
					style = Style(i);
			}
		}
		else if (value.key == "interface_size")
			interfaceSize = std::clamp(std::strtof(value.value.c_str(), nullptr),
			                           MIN_INTERFACE_SIZE, MAX_INTERFACE_SIZE);
	}
}

void Settings::Save() const
{
	const auto number = [](ph::f32 value)
	{
		char text[16];
		std::snprintf(text, sizeof(text), "%.2f", value);
		return std::string(text);
	};
	const std::string text = WriteKeyValues({{"music", number(music)},
	                                         {"effects", number(effects)},
	                                         {"fullscreen", fullscreen ? "1" : "0"},
	                                         {"style", StyleName(style)},
	                                         {"interface_size", number(interfaceSize)}});
	if (!ph::os::WriteSavedFile(FILE_NAME, text))
		PH_LOG_WARN("settings: cannot write %s", FILE_NAME);
}

void Settings::Apply(ph::os::WindowId window) const
{
	ph::audio::SetBusVolume(ph::audio::Bus::Music, music);
	ph::audio::SetBusVolume(ph::audio::Bus::Effects, effects);
	ph::audio::SetBusVolume(ph::audio::Bus::Interface, effects);
	if (ph::os::IsWindowFullscreen(window) != fullscreen)
		ph::os::SetWindowFullscreen(window, fullscreen);
}

Style Settings::ResolvedStyle(ph::os::WindowId window) const
{
	if (style != Style::Auto)
		return style;
#if PH_OS_ANDROID
	(void)window;
	return Style::Mobile;
#else
	// A desktop pretending to be a phone (pith's device simulation, touch on).
	if (ph::os::GetDeviceSimulation().touch)
		return Style::Mobile;
	#if PH_OS_WEB
	const ph::os::PixelSize size = ph::os::GetWindowPixelSize(window);
	const ph::f32 scale = std::max(ph::os::GetWindowScale(window), 1.0f);
	if (ph::f32(std::min(size.width, size.height)) / scale < 600.0f)
		return Style::Mobile;
	#else
	(void)window;
	#endif
	return Style::Desktop;
#endif
}
} // namespace sn
