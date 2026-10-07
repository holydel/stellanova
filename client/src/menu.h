#pragma once

#include "input_kind.h"
#include "ui.h"

#include <ph/audio/audio.h>
#include <ph/os/event.h>
#include <ph/os/window.h>

// The main menu (Campaign, Skirmish, Online, Settings, Credits, Quit), the settings
// page and the credits, over the splash where the build has one (content/
// art.json). Keys, a gamepad, the mouse or a finger.
namespace sn
{
struct Resources;
struct Settings;

class Menu
{
public:
	enum class Action : ph::u8
	{
		None,
		Campaign,   // the solo loop's hub (docs/solo-loop.md)
		Play,       // a skirmish on this machine
		PlayOnline, // the skirmish on the game's server
		Quit,
	};

	void Enter(Resources& resources, Settings& settings, ph::os::WindowId window);
	// A line under the title for a while, by its string's key ("" for none):
	// why the last online game ended.
	void Notice(const char* key);
	void Leave();
	// Settings, or with `credits` the credits (the playground's jumps).
	void OpenPage(bool credits);
	// The input used last, which the hint is for.
	void SetInput(InputKind kind) { input = kind; }
	Action OnEvent(const ph::os::Event& event);
	Action Update(ph::f32 dt);
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);

private:
	enum class Page : ph::u8
	{
		Main,
		Settings,
		Credits,
	};

	ph::u32 ItemCount() const;
	const char* ItemKey(ph::u32 item) const;
	void Select(ph::u32 item);
	void Move(int step);
	Action Activate();
	// Activating wraps volumes around; the arrow keys stop at the ends.
	void Adjust(int step, bool wrap = false);
	Action Back();
	// The item under a pointer at pixels (x, y), or ItemCount().
	ph::u32 ItemAt(ph::f32 x, ph::f32 y) const;
	void DrawItems(Ui& ui);
	void DrawSplash(Ui& ui);

	Resources* resources = nullptr;
	Settings* settings = nullptr;
	ph::os::WindowId window = 0;
	Page page = Page::Main;
	ph::u32 selected = 0;
	// From the last frame, for the pointer: the items' left edge in units.
	ph::f32 pixelsPerUnit = 1.0f;
	ph::f32 itemX = 96.0f;
	ph::f32 itemY = 300.0f;
	ph::f32 itemWidth = 460.0f;
	// A stick held to one side repeats, slowly then faster.
	int stickStep = 0;
	ph::f32 stickRepeat = 0.0f;
	const char* notice = "";
	ph::f32 noticeLeft = 0.0f;          // s
	InputKind input = InputKind::Mouse; // SetInput
	ph::audio::Voice ambience;
};
} // namespace sn
