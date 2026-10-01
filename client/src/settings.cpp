#include "settings.h"

#include "key_values.h"

#include <ph/audio/audio.h>
#include <ph/core/log.h>
#include <ph/os/app.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace sn
{
namespace
{
std::string Path() { return std::string(ph::os::GetDataDirectory()) + "settings.txt"; }

ph::f32 Volume(const std::string& text)
{
	return std::clamp(std::strtof(text.c_str(), nullptr), 0.0f, 1.0f);
}
} // namespace

void Settings::Load()
{
	FILE* file = std::fopen(Path().c_str(), "rb");
	if (!file)
		return; // the first run: the defaults
	std::string text;
	char buffer[1024];
	ph::usize count;
	while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
		text.append(buffer, count);
	std::fclose(file);
	for (const KeyValue& value : ParseKeyValues(text))
	{
		if (value.key == "music")
			music = Volume(value.value);
		else if (value.key == "effects")
			effects = Volume(value.value);
		else if (value.key == "fullscreen")
			fullscreen = value.value == "1";
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
	                                         {"fullscreen", fullscreen ? "1" : "0"}});
	FILE* file = std::fopen(Path().c_str(), "wb");
	if (!file)
	{
		PH_LOG_WARN("settings: cannot write %s", Path().c_str());
		return;
	}
	std::fwrite(text.data(), 1, text.size(), file);
	std::fclose(file);
}

void Settings::Apply(ph::os::WindowId window) const
{
	ph::audio::SetBusVolume(ph::audio::Bus::Music, music);
	ph::audio::SetBusVolume(ph::audio::Bus::Effects, effects);
	ph::audio::SetBusVolume(ph::audio::Bus::Interface, effects);
	if (ph::os::IsWindowFullscreen(window) != fullscreen)
		ph::os::SetWindowFullscreen(window, fullscreen);
}
} // namespace sn
