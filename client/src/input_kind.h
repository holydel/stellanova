#pragma once

#include <ph/os/event.h>

#include <cmath>

// What the player used last (docs/adr/0018-one-interface-every-device.md).
// Every device works at any time; screens only show themselves for the last
// one: a focus ring and pad prompts for a pad, hover for a mouse, bigger
// targets for touch.
namespace sn
{
enum class InputKind : ph::u8
{
	Mouse,
	Touch,
	Pad,
	Keys,
};

inline const char* InputKindName(InputKind kind)
{
	constexpr const char* NAMES[] = {"mouse", "touch", "pad", "keys"};
	return NAMES[ph::u32(kind)];
}

class InputTracker
{
public:
	void Reset(InputKind kind) { last = kind; }
	InputKind Last() const { return last; }

	// `typing`: a text field has the keys (os::IsTextInputActive), which is
	// not a change to Keys.
	void OnEvent(const ph::os::Event& event, bool typing)
	{
		using ph::os::EventType;
		switch (event.type)
		{
			case EventType::TouchDown: last = InputKind::Touch; break;
			case EventType::MouseMotion:
				// A finger moves the mouse too; a resting hand nudges it a hair.
				if (event.motion.touch)
					last = InputKind::Touch;
				else if (std::fabs(event.motion.dx) + std::fabs(event.motion.dy) > 2.0f)
					last = InputKind::Mouse;
				break;
			case EventType::MouseButtonDown:
				last = event.button.touch ? InputKind::Touch : InputKind::Mouse;
				break;
			case EventType::MouseWheel: last = InputKind::Mouse; break;
			case EventType::GamepadButtonDown: last = InputKind::Pad; break;
			case EventType::GamepadAxisMoved:
				if (std::fabs(event.gamepad.value) > 0.5f)
					last = InputKind::Pad;
				break;
			case EventType::KeyDown:
				if (!typing && !event.key.repeat)
					last = InputKind::Keys;
				break;
			default: break;
		}
	}

private:
	InputKind last = InputKind::Mouse;
};
} // namespace sn
