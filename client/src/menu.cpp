#include "menu.h"

#include "resources.h"
#include "settings.h"

#include <ph/core/log.h>
#include <ph/os/input.h>
#include <ph/render/camera.h>
#include <ph/render/frame.h>
#include <ph/render/sky.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sn
{
namespace
{
using namespace ph;
using namespace ph::os;

constexpr const char* MAIN_ITEMS[] = {"menu.skirmish", "menu.online", "menu.settings",
                                      "menu.credits", "menu.quit"};
constexpr const char* SETTINGS_ITEMS[] = {"settings.music", "settings.effects",
                                          "settings.fullscreen", "menu.back"};
constexpr const char* CREDITS_ITEMS[] = {"menu.back"};
// What the game shows from its content (content/README.md).
constexpr const char* CREDITS[] = {"credits.sky", "credits.fonts", "credits.sounds",
                                   "credits.engine"};
constexpr u32 SKIRMISH = 0;
constexpr u32 ONLINE = 1;
constexpr u32 SETTINGS = 2;
constexpr u32 CREDITS_ITEM = 3;
constexpr u32 QUIT = 4;
constexpr ph::f32 NOTICE_SECONDS = 6.0f;
constexpr u32 MUSIC = 0;
constexpr u32 EFFECTS = 1;
constexpr u32 FULLSCREEN = 2;

// The items' place on screen, in units (the left margin narrows with the
// screen).
constexpr f32 MARGIN = 96.0f;
constexpr f32 ITEM_Y = 300.0f;
constexpr f32 ITEM_STEP = 60.0f;
constexpr f32 ITEM_WIDTH = 460.0f;
constexpr f32 TEXT_SIZE = 32.0f;

const u32 TEXT = render::PackColor(0.78f, 0.82f, 0.9f);
const u32 FAINT = render::PackColor(0.45f, 0.5f, 0.6f);
const u32 ACCENT = render::PackColor(1.0f, 0.78f, 0.35f);
const u32 GLOW = render::PackColor(1.0f, 0.45f, 0.1f, 0.6f);
} // namespace

void Menu::Enter(Resources& from, Settings& with, os::WindowId in)
{
	resources = &from;
	settings = &with;
	window = in;
	page = Page::Main;
	selected = SKIRMISH;
	if (!audio::IsPlaying(ambience))
	{
		audio::PlayDesc desc;
		desc.bus = audio::Bus::Music;
		desc.loop = true;
		desc.volume = 0.5f;
		ambience = audio::Play(resources->ambience, desc);
	}
}

void Menu::Leave() { audio::Stop(ambience); }

void Menu::Notice(const char* key)
{
	notice = key ? key : "";
	noticeLeft = notice[0] ? NOTICE_SECONDS : 0.0f;
}

u32 Menu::ItemCount() const
{
	switch (page)
	{
		case Page::Main: return u32(std::size(MAIN_ITEMS));
		case Page::Settings: return u32(std::size(SETTINGS_ITEMS));
		case Page::Credits: return u32(std::size(CREDITS_ITEMS));
	}
	return 0;
}

const char* Menu::ItemKey(u32 item) const
{
	switch (page)
	{
		case Page::Main: return MAIN_ITEMS[item];
		case Page::Settings: return SETTINGS_ITEMS[item];
		case Page::Credits: return CREDITS_ITEMS[item];
	}
	return "";
}

void Menu::Select(u32 item)
{
	if (item == selected || item >= ItemCount())
		return;
	selected = item;
	resources->Play(resources->move);
}

void Menu::Move(int step) { Select((selected + ItemCount() + u32(step)) % ItemCount()); }

Menu::Action Menu::Activate()
{
	if (page == Page::Credits)
		return Back();
	if (page == Page::Settings)
	{
		if (selected == u32(std::size(SETTINGS_ITEMS)) - 1)
			return Back();
		Adjust(1, true);
		return Action::None;
	}
	resources->Play(resources->confirm);
	switch (selected)
	{
		case SKIRMISH: return Action::Play;
		case ONLINE: return Action::PlayOnline;
		case SETTINGS:
			PH_LOG_INFO("menu: settings");
			page = Page::Settings;
			selected = 0;
			return Action::None;
		case CREDITS_ITEM:
			page = Page::Credits;
			selected = 0;
			return Action::None;
		case QUIT: return Action::Quit;
		default: return Action::None;
	}
}

// Volumes step by a tenth (and wrap when activated); fullscreen toggles.
void Menu::Adjust(int step, bool wrap)
{
	if (page != Page::Settings)
		return;
	const auto volume = [step, wrap](f32 value)
	{
		const int tenths = int(std::lround(value * 10.0f)) + step;
		if (tenths > 10 && wrap)
			return 0.0f;
		return f32(std::clamp(tenths, 0, 10)) / 10.0f;
	};
	switch (selected)
	{
		case MUSIC: settings->music = volume(settings->music); break;
		case EFFECTS: settings->effects = volume(settings->effects); break;
		case FULLSCREEN: settings->fullscreen = !settings->fullscreen; break;
		default: return;
	}
	resources->Play(resources->move);
	settings->Apply(window);
	settings->Save();
	PH_LOG_INFO("menu: music %.1f, effects %.1f, fullscreen %s", settings->music, settings->effects,
	            settings->fullscreen ? "on" : "off");
}

Menu::Action Menu::Back()
{
	if (page != Page::Main)
	{
		resources->Play(resources->back);
		selected = page == Page::Settings ? SETTINGS : CREDITS_ITEM;
		page = Page::Main;
	}
	else
		Select(QUIT);
	return Action::None;
}

u32 Menu::ItemAt(f32 x, f32 y) const
{
	const f32 ux = x / pixelsPerUnit;
	const f32 uy = y / pixelsPerUnit;
	if (ux < itemX - 24.0f || ux > itemX + itemWidth)
		return ItemCount();
	const f32 row = (uy - itemY + 0.25f * ITEM_STEP) / ITEM_STEP;
	return row >= 0.0f && row < f32(ItemCount()) ? u32(row) : ItemCount();
}

Menu::Action Menu::OnEvent(const Event& event)
{
	switch (event.type)
	{
		case EventType::KeyDown:
			switch (event.key.scancode)
			{
				case Scancode::Up:
				case Scancode::W: Move(-1); break;
				case Scancode::Down:
				case Scancode::S: Move(1); break;
				case Scancode::Left:
				case Scancode::A: Adjust(-1); break;
				case Scancode::Right:
				case Scancode::D: Adjust(1); break;
				case Scancode::Return:
				case Scancode::KpEnter:
				case Scancode::Space:
					if (!event.key.repeat)
						return Activate();
					break;
				case Scancode::Escape:
				case Scancode::Backspace:
					if (!event.key.repeat)
						return Back();
					break;
				case Scancode::AcBack: // Android's Back leaves the game from the main page
					if (page == Page::Main)
						return Action::Quit;
					return Back();
				default: break;
			}
			break;
		case EventType::GamepadButtonDown:
			switch (event.gamepad.button)
			{
				case GamepadButton::DpadUp: Move(-1); break;
				case GamepadButton::DpadDown: Move(1); break;
				case GamepadButton::DpadLeft: Adjust(-1); break;
				case GamepadButton::DpadRight: Adjust(1); break;
				case GamepadButton::South:
				case GamepadButton::Start: return Activate();
				case GamepadButton::East: return Back();
				default: break;
			}
			break;
		// A finger acts as the mouse (os/input.h): taps arrive here too.
		case EventType::MouseMotion:
		{
			const u32 item = ItemAt(event.motion.x, event.motion.y);
			if (item < ItemCount())
				Select(item);
			break;
		}
		case EventType::MouseButtonDown:
		{
			if (event.button.button != MouseButton::Left)
				break;
			const u32 item = ItemAt(event.button.x, event.button.y);
			if (item < ItemCount())
			{
				Select(item);
				return Activate();
			}
			break;
		}
		default: break;
	}
	return Action::None;
}

Menu::Action Menu::Update(f32 dt)
{
	// F11 and Alt+Enter change fullscreen too: the setting follows.
	if (IsWindowFullscreen(window) != settings->fullscreen)
	{
		settings->fullscreen = !settings->fullscreen;
		settings->Save();
	}
	GamepadState pads[MAX_GAMEPADS];
	const u32 count = GetGamepads(pads, MAX_GAMEPADS);
	f32 x = 0.0f;
	f32 y = 0.0f;
	for (u32 i = 0; i < count; ++i)
	{
		x += pads[i].Axis(GamepadAxis::LeftX);
		y += pads[i].Axis(GamepadAxis::LeftY);
	}
	ApplyDeadZone(x, y, 0.5f);
	// Up the stick is up the list; sideways adjusts a setting.
	const int step =
		std::abs(y) >= std::abs(x) ? (y > 0.0f ? -1 : (y < 0.0f ? 1 : 0)) : (x > 0.0f ? 2 : -2);
	if (step == 0)
	{
		stickStep = 0;
		return Action::None;
	}
	stickRepeat -= dt;
	if (step == stickStep && stickRepeat > 0.0f)
		return Action::None;
	stickRepeat = step == stickStep ? 0.12f : 0.4f;
	stickStep = step;
	if (step == 2 || step == -2)
		Adjust(step / 2);
	else
		Move(step);
	return Action::None;
}

void Menu::Draw(rhi::CommandList& commands, const render::FrameTime& time, Ui& ui)
{
	const f32 t = f32(time.seconds);
	// A ship turning slowly, the stars behind it: beside the items on wide
	// screens, below them on narrow ones. The camera looks past the ship so
	// that it shows there (in clip space).
	constexpr f32 FOV = 0.9f;
	constexpr f32 DISTANCE = 9.0f;
	const f32 aspect = f32(commands.size.width) / f32(commands.size.height);
	const f32 halfHeight = std::tan(0.5f * FOV) * DISTANCE;
	const Vec2 at = aspect >= 1.3f ? Vec2{0.4f, 0.0f} : Vec2{0.25f, -0.5f};
	const Vec3 target = {-at.x * halfHeight * aspect, -at.y * halfHeight, 0.0f};
	const Vec3 eye = target + Vec3{0.0f, 1.2f, DISTANCE};
	const Mat4 view = LookAt(eye, target, {0.0f, 1.0f, 0.0f});
	const render::FrameData frame = render::MakeFrameData3D(commands, time, view, eye, FOV);
	render::SetFrameData(commands, frame);
	const f32 size = aspect >= 1.3f ? 1.0f : 0.8f;
	resources->DrawShip(commands, RotationY(0.35f * t) * RotationZ(0.12f * std::sin(0.7f * t)) *
	                                  Scale({size, size, size}));
	if (resources->sky)
		render::DrawSky(commands, resources->sky, frame, 1.0f);

	ui.Begin(commands, time, *resources);
	pixelsPerUnit = ui.PixelsPerUnit();
	itemX = std::min(MARGIN, 0.1f * ui.Width());
	StringTable& strings = resources->strings;
	TextLook title;
	title.size = 88.0f;
	title.bold = true;
	title.color = render::PackColor(1.0f, 0.92f, 0.7f);
	title.glow = 0.07f;
	title.glowColor = GLOW;
	// Smaller where it would not fit.
	const f32 room = ui.Width() - 2.0f * itemX;
	itemWidth = std::min(ITEM_WIDTH, room);
	const f32 wide = ui.Measure(strings.Get("game.title"), title).x;
	if (wide > room)
		title.size *= room / wide;
	ui.Text(strings.Get("game.title"), itemX - 6.0f, 110.0f, title);
	TextLook small;
	small.size = 18.0f;
	small.color = FAINT;
	const char* subtitle = page == Page::Main       ? "menu.subtitle"
	                       : page == Page::Settings ? "settings.title"
	                                                : "credits.title";
	ui.Text(strings.Get(subtitle), itemX, 215.0f, small);
	// Why the last online game ended, fading after a while.
	noticeLeft = std::max(0.0f, noticeLeft - f32(time.delta));
	if (page == Page::Main && noticeLeft > 0.0f)
	{
		TextLook warning = small;
		warning.color = render::PackColor(1.0f, 0.55f, 0.45f, std::min(1.0f, noticeLeft));
		ui.Text(strings.Get(notice), itemX, 242.0f, warning);
	}
	itemY = ITEM_Y;
	if (page == Page::Credits)
	{
		// The lines, then Back below them.
		TextLook line;
		line.size = 20.0f;
		line.color = TEXT;
		// Clear of the ship, which turns on the right half of wide screens.
		line.maxWidth = aspect >= 1.3f ? 0.5f * ui.Width() - itemX : room;
		for (const char* key : CREDITS)
			itemY += ui.Text(strings.Get(key), itemX, itemY, line).y + 14.0f;
		itemY += 24.0f;
	}
	DrawItems(ui);
	small.maxWidth = room;
	const Vec2 hint = ui.Measure(strings.Get("menu.hint"), small);
	ui.Text(strings.Get("menu.hint"), itemX, Ui::HEIGHT - 32.0f - hint.y, small);
	small.align = Align::Right;
	small.maxWidth = 0.0f;
	ui.Text("v" SN_VERSION, ui.Width() - 24.0f, 24.0f, small);
	ui.End(commands);
}

void Menu::DrawItems(Ui& ui)
{
	StringTable& strings = resources->strings;
	for (u32 i = 0; i < ItemCount(); ++i)
	{
		const f32 y = itemY + f32(i) * ITEM_STEP;
		const bool on = i == selected;
		TextLook look;
		look.size = TEXT_SIZE;
		look.color = on ? ACCENT : TEXT;
		look.glow = on ? 0.06f : 0.0f;
		look.glowColor = GLOW;
		if (on)
			ui.Box(itemX - 24.0f, y + 6.0f, 6.0f, TEXT_SIZE, ACCENT);
		ui.Text(strings.Get(ItemKey(i)), itemX, y, look);
		if (page != Page::Settings || i > FULLSCREEN)
			continue;
		char value[32];
		if (i == FULLSCREEN)
			std::snprintf(value, sizeof(value), "%s",
			              strings.Get(settings->fullscreen ? "settings.on" : "settings.off"));
		else
			std::snprintf(
				value, sizeof(value), "%d%%",
				int(std::lround((i == MUSIC ? settings->music : settings->effects) * 100.0f)));
		look.align = Align::Right;
		ui.Text(value, itemX + itemWidth, y, look);
	}
}
} // namespace sn
