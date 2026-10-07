#include "hub.h"

#include "icon_codes.h"
#include "resources.h"

#include <sn/sim/catalog.h>
#include <sn/sim/fitting.h>

#include <ph/core/log.h>
#include <ph/core/profile.h>
#include <ph/core/time.h>
#include <ph/os/app.h>
#include <ph/platform/platform.h>
#include <ph/render/shapes.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

namespace sn
{
namespace
{
using namespace ph;
using namespace ph::os;

// The screen in units at least: dense, as a strategy game's (the developer,
// 2026-10-05: smaller elements, thinner lines).
constexpr f32 DESIGN_WIDTH = 1600.0f;
constexpr f32 DESIGN_HEIGHT = 900.0f;
// The mobile style (docs/adr/0018-one-interface-every-device.md) takes the
// compact layout: the tabs in a bar at the bottom, panels one under another
// and finger-sized buttons (units near the phone's dp: Hub::SetDensity).
constexpr f32 TOUCH_SIZE = 48.0f;
constexpr f32 TAB_HEIGHT = 58.0f;
constexpr f32 NOTICE_SECONDS = 4.0f;
constexpr const char* KEY_FILE = "account.txt";
// Steam's turn before the Login: the platform connecting, then its ticket.
// Past these, the Login goes as a guest's (and signs in when Steam comes).
constexpr f64 PLATFORM_WAIT = 4.0;
constexpr f64 TICKET_WAIT = 6.0;
constexpr const char* STEAM_SERVICE = "stellanova"; // the server checks tickets under it
constexpr const char* BUILDING_KEYS[sim::BUILDINGS] = {
	"building.mine", "building.extractor", "building.fab", "building.depot", "building.command"};
constexpr const char* RESOURCE_KEYS[sim::RESOURCES] = {"resource.metal", "resource.he3",
                                                       "resource.chips"};
const u32 METAL = ui::Color(0xb8c4d6);
const u32 HE3 = ui::Color(0x7fe0ff);
const u32 CHIPS = ui::Color(0x9cff8a);
const u32 RESOURCE_COLORS[sim::RESOURCES] = {METAL, HE3, CHIPS};
const u32 WARN = ui::Color(0xff6b5b);
const u32 GOOD = ui::Color(0x5ee38a);
const u32 GOLD = ui::Color(0xffc857);

ui::Layout Panel(ui::Size width, ui::Size height)
{
	ui::Layout layout;
	layout.width = width;
	layout.height = height;
	layout.padding = ui::All(14.0f);
	layout.gap = 8.0f;
	return layout;
}

ui::Layout Row(f32 gap = 8.0f)
{
	ui::Layout layout;
	layout.width = ui::Grow();
	layout.gap = gap;
	layout.align = ui::Align::Center;
	return layout;
}

// Type sizes in units, by style: phones are small in the hand, so their
// type is bigger (and their screens show fewer words: no hints).
struct TypeSizes
{
	f32 text;
	f32 small;
	f32 heading;
	f32 title; // panels' and cards' names
};

constexpr TypeSizes DESKTOP_TYPE = {16.0f, 13.0f, 22.0f, 17.0f};
constexpr TypeSizes MOBILE_TYPE = {20.0f, 16.0f, 26.0f, 21.0f};
f32 gTitleSize = DESKTOP_TYPE.title; // this frame's

// A panel's or a card's name: small and light, as the reference's.
ui::TextLook Title()
{
	ui::TextLook look;
	look.size = gTitleSize;
	return look;
}

ui::TextLook Note(const ui::Theme& theme, u32 color = 0)
{
	ui::TextLook look;
	look.size = theme.smallSize;
	look.color = color ? color : theme.textDim;
	return look;
}

std::string Number(f64 value, const char* format = "%.0f")
{
	char text[32];
	std::snprintf(text, sizeof(text), format, value);
	return text;
}

// "1:05:30", "12:04", "0:09".
std::string Duration(i64 seconds)
{
	seconds = std::max<i64>(0, seconds);
	char text[32];
	if (seconds >= 3600)
		std::snprintf(text, sizeof(text), "%lld:%02lld:%02lld", (long long)(seconds / 3600),
		              (long long)(seconds / 60 % 60), (long long)(seconds % 60));
	else
		std::snprintf(text, sizeof(text), "%lld:%02lld", (long long)(seconds / 60),
		              (long long)(seconds % 60));
	return text;
}

} // namespace

std::string Hub::DeviceKey()
{
	std::string key;
	if (ReadSavedFile(KEY_FILE, key))
	{
		while (!key.empty() && (key.back() == '\n' || key.back() == '\r' || key.back() == ' '))
			key.pop_back();
		if (key.size() == sim::KEY_BYTES)
			return key;
	}
	// 128 random bits, as hex: the device's from now on.
	std::random_device device;
	std::mt19937_64 mix(u64(device()) << 32 ^ u64(device()) ^
	                    u64(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	key.clear();
	for (u32 i = 0; i < sim::KEY_BYTES; ++i)
		key += "0123456789abcdef"[mix() % 16];
	KeepKey(key);
	return key;
}

void Hub::KeepKey(const std::string& key)
{
	if (!WriteSavedFile(KEY_FILE, key))
		PH_LOG_WARN("hub: the account's key could not be kept");
}

void Hub::Enter(Resources& from, Connection& through)
{
	resources = &from;
	connection = &through;
	if (!themed)
	{
		themed = true;
		ui::Theme& theme = ui.GetTheme();
		theme.regular = &resources->sans;
		theme.bold = &resources->bold;
		// The house style (the developer's reference, 2026-10-05: EVE Online's
		// dashboards): near-black glass panels, hairlines, small light type,
		// cyan the only accent; primary buttons tinted, not filled. Type
		// sizes follow the style (Draw).
		theme.radius = 2.0f;
		theme.cut = false;
		theme.text = ui::Color(0xd4dde6);
		theme.textDim = ui::Color(0x758493);
		theme.panel = ui::Color(0x0a0f15, 0.84f);
		theme.panelBorder = ui::Color(0x223140);
		theme.surface = ui::Color(0x111a24, 0.92f);
		theme.surfaceHover = ui::Color(0x1a2a3c);
		theme.surfacePressed = ui::Color(0x0b1219);
		theme.accent = ui::Color(0x4ec6de);
		theme.accentText = ui::Color(0x041016);
		theme.grid = ui::Color(0x1b2733);
		theme.primaryFill = 0.16f;
		theme.primaryText = theme.accent;
		theme.progressHeight = 4.0f;
		theme.selectedFill = 0.14f;
	}
	launching = false;
	told = false;
	provider.clear();
	loginWait = 0.0;
	// A local practice campaign takes no sign-in.
	login = LoginState::Platform;
	if (connection->IsLocal())
		SendLogin({});
}

void Hub::UpdateLogin()
{
	switch (login)
	{
		case LoginState::Platform:
		{
			const AsyncStatus status = platform::Poll();
			if (status == AsyncStatus::Ready)
			{
				platform::RequestAuthTicket(STEAM_SERVICE);
				login = LoginState::Ticket;
				loginWait = 0.0;
			}
			else if (status == AsyncStatus::Failed || loginWait > PLATFORM_WAIT)
			{
				SendLogin({});
				login = status == AsyncStatus::Failed ? LoginState::Sent : LoginState::Guest;
			}
			break;
		}
		case LoginState::Ticket:
		case LoginState::LateTicket:
		{
			std::string ticket;
			const AsyncStatus status = platform::PollAuthTicket(ticket);
			if (status == AsyncStatus::Ready)
				SendLogin(ticket);
			else if (status == AsyncStatus::Failed || loginWait > TICKET_WAIT)
			{
				if (login == LoginState::Ticket)
					SendLogin({});
				PH_LOG_INFO("hub: no Steam ticket: a guest");
			}
			else
				break;
			login = LoginState::Sent;
			break;
		}
		case LoginState::Guest:
			// Steam came up after the guest's Login: sign in now.
			if (told && provider.empty() && platform::Poll() == AsyncStatus::Ready)
			{
				platform::RequestAuthTicket(STEAM_SERVICE);
				login = LoginState::LateTicket;
				loginWait = 0.0;
			}
			else if (platform::Poll() == AsyncStatus::Failed)
				login = LoginState::Sent;
			break;
		case LoginState::Sent: break;
	}
}

void Hub::SendLogin(const std::string& ticket)
{
	sim::Login message;
	message.key = DeviceKey();
	message.name = sim::CleanText(platform::GetPlayerName(), sim::MAX_NAME_BYTES);
	if (!ticket.empty())
	{
		message.provider = "steam";
		message.proof = ticket;
	}
	PH_LOG_INFO("hub: logging in%s", ticket.empty() ? "" : " with Steam");
	connection->Post(sim::Write(message), net::Delivery::Reliable);
	login = LoginState::Sent;
}

void Hub::ShowTab(const char* name)
{
	const std::string_view wanted = name;
	tab = wanted == "hangar" || wanted == "fitting"      ? Tab::Hangar
	      : wanted == "storage" || wanted == "inventory" ? Tab::Storage
	      : wanted == "map"                              ? Tab::Map
	                                                     : Tab::Colony;
}

void Hub::Leave()
{
	launching = false;
	leaving = false;
}

i64 Hub::Now() const { return serverNow + i64(f64(MonotonicNs()) * 1e-9 - receivedAt); }

void Hub::Request(const sim::Operation& operation)
{
	connection->Post(sim::Write(sim::Request{sim::ToJson(operation)}), net::Delivery::Reliable);
	resources->Play(resources->confirm);
}

Hub::Action Hub::OnEvent(const Event& event)
{
	const bool took = ui.OnEvent(event);
	// The map: a drag pans it, a click picks a node, the wheel zooms.
	const auto inMap = [this](f32 x, f32 y)
	{
		const Vec2 at = {x * unitsPerPixel, y * unitsPerPixel};
		return tab == Tab::Map && !reporting && at.x >= mapMin.x && at.x < mapMax.x &&
		       at.y >= mapMin.y && at.y < mapMax.y;
	};
	// The colony's scene: the building under the pointer; a click picks it.
	const auto inScene = [this](f32 x, f32 y)
	{
		const Vec2 at = {x * unitsPerPixel, y * unitsPerPixel};
		return ShowsColony() && at.x >= sceneMin.x && at.x < sceneMax.x && at.y >= sceneMin.y &&
		       at.y < sceneMax.y;
	};
	if (event.type == EventType::MouseMotion)
		hovered = inScene(event.motion.x, event.motion.y)
		              ? colony.Pick({event.motion.x, event.motion.y})
		              : -1;
	if (event.type == EventType::MouseButtonUp && event.button.button == MouseButton::Left &&
	    inScene(event.button.x, event.button.y))
	{
		const i32 under = colony.Pick({event.button.x, event.button.y});
		if (under != picked)
			resources->Play(resources->move);
		picked = under;
	}
	switch (event.type)
	{
		case EventType::MouseButtonDown:
			if (event.button.button == MouseButton::Left && inMap(event.button.x, event.button.y))
			{
				mapPressed = true;
				mapDragged = false;
				pressAt = {event.button.x * unitsPerPixel, event.button.y * unitsPerPixel};
				panAt = pan;
			}
			break;
		case EventType::MouseMotion:
			if (mapPressed)
			{
				const Vec2 at = {event.motion.x * unitsPerPixel, event.motion.y * unitsPerPixel};
				if (sim::Length(at - pressAt) > 6.0f)
					mapDragged = true;
				if (mapDragged)
					pan = panAt + (at - pressAt);
			}
			break;
		case EventType::MouseButtonUp:
			if (mapPressed && !mapDragged && event.button.button == MouseButton::Left)
			{
				const sim::Hex hex =
					HexAt({event.button.x * unitsPerPixel, event.button.y * unitsPerPixel});
				if (have && sim::IsVisible(sim::GetCatalog(), shown, hex) && sim::RingOf(hex) > 0)
				{
					node = hex;
					resources->Play(resources->move);
				}
			}
			mapPressed = false;
			break;
		case EventType::MouseWheel:
			if (tab == Tab::Map)
				zoom = std::clamp(zoom * (event.wheel.y > 0.0f ? 1.15f : 1.0f / 1.15f), 0.4f, 2.5f);
			break;
		case EventType::KeyDown:
			if (!took && !event.key.repeat &&
			    (event.key.scancode == Scancode::Escape || event.key.scancode == Scancode::AcBack))
			{
				if (reporting)
					reporting = false;
				else
					return Action::Leave;
			}
			break;
		case EventType::GamepadButtonDown:
			if (!took && event.gamepad.button == GamepadButton::East)
			{
				if (reporting)
					reporting = false;
				else
					return Action::Leave;
			}
			break;
		default: break;
	}
	return Action::None;
}

void Hub::OnMessage(const u8* data, u32 size)
{
	switch (sim::TypeOf(data, size))
	{
		case sim::MessageType::Signed:
		{
			sim::Signed answer;
			if (!sim::Read(data, size, answer))
				break;
			// A key handed out for this device's account replaces its own.
			if (!answer.key.empty() && answer.key != DeviceKey())
				KeepKey(answer.key);
			provider = answer.provider;
			told = true;
			if (!answer.error.empty())
			{
				notice = answer.error;
				noticeLeft = NOTICE_SECONDS;
			}
			PH_LOG_INFO("hub: %s%s%s", provider.empty() ? "a guest" : "signed in with ",
			            provider.c_str(), answer.error.empty() ? "" : " (the sign-in failed)");
			break;
		}
		case sim::MessageType::Profile:
		{
			sim::Profile profile;
			sim::Account read;
			i64 now = 0;
			if (!sim::Read(data, size, profile) || !sim::ReadProfile(profile.json, read, now))
				break;
			account = std::move(read);
			serverNow = now;
			receivedAt = f64(MonotonicNs()) * 1e-9;
			connection->SetAnswered();
			if (!have)
				PH_LOG_INFO("hub: account %s, %u ships, deepest ring %u", account.name.c_str(),
				            u32(account.ships.size()), account.deepest);
			have = true;
			// The picks follow the account: ships gone are no longer picked.
			std::erase_if(fleet, [this](u32 id)
			              { return !account.FindShip(id) || account.FindShip(id)->away; });
			// --battle: the first ship at home, at the first node in reach.
			if (autoLaunch && !launching)
			{
				autoLaunch = false;
				for (const sim::ShipRecord& record : account.ships)
				{
					if (record.away)
						continue;
					sim::Operation operation;
					operation.kind = sim::Operation::Kind::Launch;
					operation.node = {1, 0};
					operation.ships = {record.id};
					Request(operation);
					launching = true;
					break;
				}
			}
			break;
		}
		case sim::MessageType::Result:
		{
			sim::Result result;
			if (sim::Read(data, size, result) && sim::FromJson(result.json, report))
			{
				reporting = true;
				launching = false;
				tab = Tab::Map;
			}
			break;
		}
		case sim::MessageType::Chat:
		{
			sim::Chat chat;
			if (sim::Read(data, size, chat) && chat.notice)
			{
				notice = chat.text;
				noticeLeft = NOTICE_SECONDS;
				launching = false;
				resources->Play(resources->back);
			}
			break;
		}
		default: break;
	}
}

bool Hub::CanPay(const sim::Resources& cost) const
{
	for (u32 r = 0; r < sim::RESOURCES; ++r)
	{
		if (shown.resources.amount[r] + 1e-6 < cost.amount[r])
			return false;
	}
	return true;
}

std::string Hub::NameOf(const char* prefix, const std::string& id)
{
	const std::string key = std::string(prefix) + "." + id;
	const char* name = resources->strings.Get(key.c_str());
	const char* mark = icon::Find(id);
	return *mark ? std::string(mark) + " " + name : std::string(name);
}

void Hub::CostLine(const sim::Resources& cost)
{
	const ui::Theme& theme = ui.GetTheme();
	ui.BeginRow(Row(10.0f));
	for (u32 r = 0; r < sim::RESOURCES; ++r)
	{
		if (cost.amount[r] <= 0.0)
			continue;
		const bool lacking = shown.resources.amount[r] + 1e-6 < cost.amount[r];
		const std::string text =
			Number(cost.amount[r]) + " " + resources->strings.Get(RESOURCE_KEYS[r]);
		ui.Text(text.c_str(), Note(theme, lacking ? WARN : RESOURCE_COLORS[r]));
	}
	ui.End();
}

void Hub::Draw(rhi::CommandList& commands, const render::FrameTime& time)
{
	PH_PROFILE_SCOPE("Hub.Draw");
	render::UpdateTextFonts();
	const f32 dt = f32(time.delta);
	noticeLeft = std::max(0.0f, noticeLeft - dt);
	loginWait += f64(dt);
	UpdateLogin();
	if (have)
	{
		shown = account;
		sim::Advance(sim::GetCatalog(), shown, Now());
	}
	const f32 width = f32(commands.size.width);
	const f32 height = f32(commands.size.height);
	// The design fits the screen; on a phone, units are never smaller than
	// its dp (the OS's density), or the text would be too small to read. The
	// player's interface size scales that.
	const f32 scale =
		std::max(std::min(width / DESIGN_WIDTH, height / DESIGN_HEIGHT), density) * sizeFactor;
	unitsPerPixel = 1.0f / scale;
	const Vec2 area = {width / scale, height / scale};
	compact = mobileStyle;
	ui::Theme& theme = ui.GetTheme();
	const TypeSizes& type = compact ? MOBILE_TYPE : DESKTOP_TYPE;
	theme.textSize = type.text;
	theme.smallSize = type.small;
	theme.headingSize = type.heading;
	gTitleSize = type.title;
	theme.border = 1.0f / scale; // a pixel
	theme.touchSize = compact ? TOUCH_SIZE : 0.0f;
	theme.scrollbar = compact ? 10.0f : 6.0f;
	ui.BeginFrame(area, scale, dt);
	Build();
	ui.EndFrame();
	const bool scene = ShowsColony();
	if (scene)
		colony.Draw(commands, time, *resources, shown, hovered, picked, sceneMin * scale,
		            sceneMax * scale);
	render::BeginShapes(commands, time, area, scale);
	// The splash, dimmed, behind it all: cut to fill, never stretched.
	if (scene)
	{
	}
	else if (resources->HasSplash())
	{
		const f32 screen = area.x / area.y;
		const f32 image = f32(resources->splashWidth) / f32(resources->splashHeight);
		Vec4 uv = {0.0f, 0.0f, 1.0f, 1.0f};
		if (screen > image)
			uv = {0.0f, 0.5f * (1.0f - image / screen), 1.0f, 0.5f * (1.0f + image / screen)};
		else
			uv = {0.5f * (1.0f - screen / image), 0.0f, 0.5f * (1.0f + screen / image), 1.0f};
		render::AddImage({0.0f, 0.0f}, area, resources->splashTexture, ui::Color(0x30384a), uv);
	}
	else
		render::AddBox({0.0f, 0.0f}, area, {.fill = ui::Color(0x070b14)});
	// The fitting screen's ship, over the background and under the interface.
	Vec2 shipMin;
	Vec2 shipMax;
	if (have && tab == Tab::Hangar && !reporting && fitting.ShipRegion(shipMin, shipMax))
	{
		render::EndShapes(commands);
		shipView.Draw(commands, time, *resources, shipMin * scale, shipMax * scale);
		render::BeginShapes(commands, time, area, scale);
	}
	ui.Draw();
	render::EndShapes(commands);
}

void Hub::Build()
{
	ui::Layout root;
	root.width = ui::Grow();
	root.height = ui::Grow();
	// Clear of a phone's bars.
	const f32 edge = compact ? 8.0f : 16.0f;
	root.padding = {edge + insets.left * unitsPerPixel, edge + insets.top * unitsPerPixel,
	                edge + insets.right * unitsPerPixel, edge + insets.bottom * unitsPerPixel};
	root.gap = compact ? 8.0f : 12.0f;
	ui.BeginColumn(root);
	if (!have)
	{
		BuildWaiting();
		ui.End();
		return;
	}
	BuildTop();
	// A guest is told what it misses, under the bar (not the practice colony
	// on this machine: it is kept, and takes no sign-in).
	if (provider.empty() && told && !connection->IsLocal())
		ui.Text(resources->strings.Get(platform::GetPlatform() == platform::Platform::Steam
										   ? "hub.guest_steam"
										   : "hub.guest_hint"),
		        Note(ui.GetTheme(), GOLD));
	if (reporting)
		BuildReport();
	else
	{
		switch (tab)
		{
			case Tab::Colony: BuildColony(); break;
			case Tab::Hangar: BuildHangar(); break;
			case Tab::Storage: BuildStorage(); break;
			case Tab::Map: BuildMap(); break;
		}
	}
	if (noticeLeft > 0.0f)
	{
		ui::TextLook look = Title();
		look.color = ui::Fade(WARN, std::min(1.0f, noticeLeft));
		look.wrap = compact;
		ui.Text(resources->strings.Get(notice.c_str()), look);
	}
	if (compact)
		TabButtons();
	ui.End();
}

void Hub::BuildWaiting()
{
	const ui::Theme& theme = ui.GetTheme();
	ui.Spacer();
	ui::Layout middle = Row();
	middle.justify = ui::Justify::Center;
	ui.BeginRow(middle);
	ui.BeginPanel(Panel(ui::Fit(), ui::Fit()));
	ui.Text(resources->strings.Get("hub.title"), Title());
	ui.Text(resources->strings
	            .Format(connection->IsLocal() ? "hub.local" : "flight.connecting",
	                    connection->HostName().c_str())
	            .c_str(),
	        Note(theme));
	ui.End();
	ui.End();
	ui.Spacer();
}

void Hub::BuildTop()
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	ui.BeginRow(Row(compact ? 8.0f : 12.0f));
	ui.BeginColumn();
	ui::TextLook name = Title();
	name.size = compact ? 20.0f : 24.0f;
	ui.Text(shown.name.c_str(), name);
	if (connection->IsLocal())
	{
		if (!compact) // phones: fewer words
			ui.Text(strings.Get("hub.practice"), Note(theme));
	}
	else if (provider.empty())
		ui.Text(strings.Get("hub.guest"), Note(theme, WARN));
	else
	{
		const std::string by = std::string("signin.") + provider;
		ui.Text(strings.Format("hub.signed", strings.Get(by.c_str())).c_str(), Note(theme, GOOD));
	}
	ui.End();
	// A phone: the name and Leave on a line, the resources on the next (the
	// tabs are at the bottom).
	if (compact)
	{
		ui.Spacer();
		if (ui.Button(strings.Get("hub.leave"), ui::ButtonKind::Ghost))
			leaving = true;
		ui.End();
		ui::Layout line = Row(6.0f);
		line.justify = ui::Justify::SpaceBetween;
		ui.BeginRow(line);
	}
	else
		ui.Spacer(0.2f);
	// Resources: amount, storage, and production an hour.
	const f64 cap = sim::StorageOf(catalog, shown.levels[u32(sim::Building::Depot)]);
	for (u32 r = 0; r < sim::RESOURCES; ++r)
	{
		ui.BeginColumn();
		const std::string glyph = icon::Find(sim::ResourceId(sim::Resource(r)));
		const std::string amount =
			glyph + " " + Number(shown.resources.amount[r]) + (compact ? "/" : " / ") + Number(cap);
		ui::TextLook look = Title();
		look.size = compact ? 15.0f : 17.0f;
		look.color = RESOURCE_COLORS[r];
		ui.Text(amount.c_str(), look);
		const sim::Building producer = r == 0   ? sim::Building::Mine
		                               : r == 1 ? sim::Building::Extractor
		                                        : sim::Building::Fab;
		const std::string perHour =
			"+" + Number(sim::ProductionPerHour(catalog, producer, shown.levels[u32(producer)])) +
			strings.Get("hub.per_hour");
		const std::string rate =
			compact ? perHour : std::string(strings.Get(RESOURCE_KEYS[r])) + " " + perHour;
		ui.Text(rate.c_str(), Note(theme));
		ui.End();
	}
	// Command points, and when the next comes.
	ui.BeginColumn();
	const u32 commandLevel = shown.levels[u32(sim::Building::Command)];
	const f64 points = sim::CommandCap(catalog, commandLevel);
	ui::TextLook look = Title();
	look.size = compact ? 15.0f : 17.0f;
	look.color = GOLD;
	const std::string command = std::string(icon::COMMAND_POINTS) + " " +
	                            Number(std::floor(shown.command)) + (compact ? "/" : " / ") +
	                            Number(points);
	ui.Text(command.c_str(), look);
	// While points come back, when the next does: in place of the label, so
	// that the bar keeps its width.
	std::string next = strings.Get("hub.command");
	const std::string wait = Duration(i64((1.0 - (shown.command - std::floor(shown.command))) *
	                                      sim::CommandRefill(catalog, commandLevel)));
	if (shown.command < points)
		next = compact ? "+1 " + wait : strings.Format("hub.command_next", wait.c_str());
	else if (compact)
		next = strings.Get("hub.full");
	ui.Text(next.c_str(), Note(theme));
	ui.End();
	if (compact)
	{
		ui.End();
		return;
	}
	ui.Spacer();
	TabButtons();
	if (ui.Button(strings.Get("hub.leave"), ui::ButtonKind::Ghost))
		leaving = true;
	ui.End();
}

void Hub::TabButtons()
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const struct
	{
		Tab tab;
		const char* key;
		const char* glyph;
	} tabs[] = {{Tab::Colony, "hub.colony", "colony"},
	            {Tab::Hangar, "hub.fitting", "slots"},
	            {Tab::Storage, "hub.inventory", "cargo"},
	            {Tab::Map, "hub.map", "map"}};
	const auto pick = [&](Tab picked)
	{
		tab = picked;
		reporting = false;
		resources->Play(resources->move);
	};
	if (!compact)
	{
		for (const auto& entry : tabs)
		{
			const std::string label = std::string(strings.Get(entry.key)) + "###" + entry.key;
			if (ui.Button(label.c_str(),
			              tab == entry.tab ? ui::ButtonKind::Primary : ui::ButtonKind::Normal))
				pick(entry.tab);
		}
		return;
	}
	// A phone: a bar of big tabs at the bottom, where the thumbs are; each
	// its icon over its name.
	ui.BeginRow(Row(6.0f));
	for (const auto& entry : tabs)
	{
		const bool on = tab == entry.tab;
		ui::Layout cell;
		cell.width = ui::Grow();
		cell.height = ui::Fixed(TAB_HEIGHT);
		cell.justify = ui::Justify::Center;
		cell.align = ui::Align::Center;
		cell.gap = 1.0f;
		ui.BeginItem(entry.key, cell, on);
		ui::TextLook glyph;
		glyph.size = 22.0f;
		glyph.color = on ? theme.accent : theme.textDim;
		ui.Text(icon::Find(entry.glyph), glyph);
		ui::TextLook label;
		label.size = 13.0f;
		label.bold = on;
		label.color = on ? theme.accent : theme.text;
		ui.Text(strings.Get(entry.key), label);
		if (ui.EndItem().clicked && !on)
			pick(entry.tab);
	}
	ui.End();
}

void Hub::BuildColony()
{
	// The scene shows through above the cards; its labels and clicks are here.
	const MapView view{this};
	ui.Custom("colony", ui::Grow(), compact ? ui::Grow(0.8f) : ui::Grow(), DrawColonyLabels,
	          ui.CopyArray(&view, 1));
	ui.LastRect(ui.LastId(), sceneMin, sceneMax);
	// A phone: the cards one under the other, scrolled.
	if (compact)
	{
		ui::Layout list;
		list.width = ui::Grow();
		list.height = ui::Grow(1.2f);
		list.gap = 6.0f;
		ui.BeginScroll("cards", list);
		for (u32 b = 0; b < sim::BUILDINGS; ++b)
			BuildCard(b);
		ui.EndScroll();
		return;
	}
	ui::Layout grid = Row(12.0f);
	grid.align = ui::Align::Stretch;
	ui.BeginRow(grid);
	for (u32 b = 0; b < sim::BUILDINGS; ++b)
		BuildCard(b);
	ui.End();
}

void Hub::BuildCard(u32 b)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const i64 now = Now();
	const sim::Building building = sim::Building(b);
	const u32 level = shown.levels[b];
	ui.PushId(b);
	ui::Layout card = Panel(ui::Grow(), compact ? ui::Fit() : ui::Grow());
	if (compact)
	{
		card.padding = ui::All(10.0f);
		card.gap = 4.0f;
	}
	ui.BeginPanel(card);
	ui::TextLook title = Title();
	if (i32(b) == picked)
		title.color = GOLD;
	const std::string heading =
		std::string(icon::Find(sim::BuildingId(building))) + " " + strings.Get(BUILDING_KEYS[b]);
	const std::string levelText = strings.Format("hub.level", Number(level).c_str());
	if (compact)
	{
		ui.BeginRow(Row());
		ui.Text(heading.c_str(), title);
		ui.Spacer();
		ui.Text(levelText.c_str(), Note(theme, GOLD));
		ui.End();
	}
	else
	{
		ui.Text(heading.c_str(), title);
		ui.Text(levelText.c_str(), Note(theme, GOLD));
		ui.Separator();
	}
	// What it gives now, and at the next level.
	const auto effect = [&](u32 at)
	{
		switch (building)
		{
			case sim::Building::Mine:
			case sim::Building::Extractor:
			case sim::Building::Fab:
			{
				const sim::Resource produces = catalog.colony.buildings[b].produces;
				return Number(sim::ProductionPerHour(catalog, building, at)) + " " +
				       strings.Get(RESOURCE_KEYS[u32(produces)]) + strings.Get("hub.per_hour");
			}
			case sim::Building::Depot:
				return strings.Format("hub.storage_of",
				                      Number(sim::StorageOf(catalog, at)).c_str());
			case sim::Building::Command:
				return strings.Format("hub.points", Number(sim::CommandCap(catalog, at)).c_str(),
				                      Duration(i64(sim::CommandRefill(catalog, at))).c_str());
		}
		return std::string();
	};
	ui::TextLook wrap = Note(theme);
	wrap.wrap = true;
	const bool busy = shown.IsUpgrading(building);
	if (compact)
	{
		// Now and next on one line: the card stays short.
		std::string line = effect(level);
		if (!busy)
			line += "  \xe2\x80\xba  " + effect(level + 1); // U+203A: the font has no arrows
		ui::TextLook look = Note(theme, theme.text);
		look.wrap = true;
		ui.Text(line.c_str(), look);
	}
	else
	{
		ui.Text(effect(level).c_str(), Note(theme, theme.text));
		const std::string about = std::string("building.") + sim::BuildingId(building) + "_about";
		ui.Text(strings.Get(about.c_str()), wrap);
		ui.Spacer();
	}
	if (busy)
	{
		const f64 total = sim::UpgradeSeconds(catalog, level);
		const f64 left = f64(shown.upgradeDone[b] - now);
		ui.Text(strings.Format("hub.upgrading", Duration(i64(left)).c_str()).c_str(),
		        Note(theme, GOLD));
		ui.Progress("upgrade", f32(std::clamp(1.0 - left / total, 0.0, 1.0)), ui::Grow(), GOLD);
	}
	else
	{
		const sim::Resources cost = sim::UpgradeCost(catalog, building, level);
		const std::string takes =
			strings.Format("hub.takes", Duration(i64(sim::UpgradeSeconds(catalog, level))).c_str());
		const auto upgrade = [&]
		{
			sim::Operation operation;
			operation.kind = sim::Operation::Kind::Upgrade;
			operation.building = building;
			Request(operation);
		};
		if (compact)
		{
			// The cost and the time, the button at the right: a thumb's reach.
			ui.BeginRow(Row());
			ui.BeginColumn();
			CostLine(cost);
			ui.Text(CanPay(cost) ? takes.c_str() : strings.Get("hub.lacking"),
			        Note(theme, CanPay(cost) ? theme.textDim : WARN));
			ui.End();
			ui.Spacer();
			if (CanPay(cost) && ui.Button(strings.Get("hub.upgrade"), ui::ButtonKind::Primary))
				upgrade();
			ui.End();
		}
		else
		{
			ui.Text(strings.Format("hub.next", effect(level + 1).c_str()).c_str(), wrap);
			CostLine(cost);
			ui.Text(takes.c_str(), Note(theme));
			if (!CanPay(cost))
				ui.Text(strings.Get("hub.lacking"), Note(theme, WARN));
			else if (ui.Button(strings.Get("hub.upgrade"), ui::ButtonKind::Primary, ui::Grow()))
				upgrade();
		}
	}
	ui.End();
	ui.PopId();
}

void Hub::BuildHangar()
{
	// The fitting screen (docs/adr/0017-inventory.md): its drops and buttons
	// ask the server; a drop that cannot be says why.
	FittingScreen::Asks asks;
	fitting.Build(ui, *resources, shown, asks, compact);
	for (const sim::Operation& operation : asks.requests)
		Request(operation);
	if (!asks.notice.empty())
	{
		notice = asks.notice;
		noticeLeft = NOTICE_SECONDS;
	}
}

void Hub::BuildStorage()
{
	// The inventory (docs/adr/0017-inventory.md): its drops and buttons ask
	// the server; a drop that cannot be says why.
	Inventory::Asks asks;
	inventory.Build(ui, *resources, shown, asks, compact);
	for (const sim::Operation& operation : asks.requests)
		Request(operation);
	if (!asks.notice.empty())
	{
		notice = asks.notice;
		noticeLeft = NOTICE_SECONDS;
	}
}

Vec2 Hub::HexCenter(sim::Hex hex, Vec2 min, Vec2 max) const
{
	const f32 size = HexSize();
	const Vec2 middle = (min + max) * 0.5f + pan;
	return {middle.x + size * 1.7320508f * (f32(hex.q) + 0.5f * f32(hex.r)),
	        middle.y + size * 1.5f * f32(hex.r)};
}

void Hub::DrawColonyLabels(ui::Context& ui, const void* data, Vec2 min, Vec2 max)
{
	const Hub& hub = *static_cast<const MapView*>(data)->hub;
	StringTable& strings = hub.resources->strings;
	render::PushClip(min, max);
	const i64 now = hub.Now();
	for (u32 b = 0; b < sim::BUILDINGS; ++b)
	{
		// Only the building hovered or picked, and those being upgraded: the
		// cards below name them all.
		const bool shown = i32(b) == hub.picked || i32(b) == hub.hovered ||
		                   hub.shown.IsUpgrading(sim::Building(b));
		Vec2 foot;
		f32 height = 0.0f;
		if (!shown || !hub.colony.Project(b, foot, height))
			continue;
		// Pixels to units, and a little above the roof.
		const Vec2 top = {foot.x * hub.unitsPerPixel,
		                  (foot.y - height) * hub.unitsPerPixel - 44.0f};
		ui::TextLook look = Title();
		look.size = 16.0f;
		look.color = i32(b) == hub.picked || i32(b) == hub.hovered ? GOLD : ui::Color(0xdde8f4);
		const std::string name = std::string(icon::Find(sim::BuildingId(sim::Building(b)))) + " " +
		                         strings.Get(BUILDING_KEYS[b]);
		const Vec2 nameSize = ui.MeasureText(name.c_str(), look);
		ui.DrawText(name.c_str(), {top.x - 0.5f * nameSize.x, top.y}, look);
		ui::TextLook small = look;
		small.size = 13.0f;
		small.color = hub.shown.IsUpgrading(sim::Building(b)) ? GOLD : ui::Color(0x8fa6c0);
		const std::string line =
			hub.shown.IsUpgrading(sim::Building(b))
		        ? strings.Format("hub.upgrading",
		                         Duration(std::max<i64>(0, hub.shown.upgradeDone[b] - now)).c_str())
		        : strings.Format("hub.level", Number(hub.shown.levels[b]).c_str());
		const Vec2 lineSize = ui.MeasureText(line.c_str(), small);
		ui.DrawText(line.c_str(), {top.x - 0.5f * lineSize.x, top.y + nameSize.y}, small);
	}
	render::PopClip();
}

sim::Hex Hub::HexAt(Vec2 point) const
{
	const f32 size = HexSize();
	const Vec2 middle = (mapMin + mapMax) * 0.5f + pan;
	const f32 x = (point.x - middle.x) / size;
	const f32 y = (point.y - middle.y) / size;
	// Fractional axial coordinates, rounded through cube coordinates.
	const f32 q = 0.57735027f * x - y / 3.0f;
	const f32 r = 2.0f / 3.0f * y;
	const f32 s = -q - r;
	f32 rq = std::round(q);
	f32 rr = std::round(r);
	const f32 rs = std::round(s);
	const f32 dq = std::fabs(rq - q);
	const f32 dr = std::fabs(rr - r);
	const f32 ds = std::fabs(rs - s);
	if (dq > dr && dq > ds)
		rq = -rr - rs;
	else if (dr > ds)
		rr = -rq - rs;
	return {i32(rq), i32(rr)};
}

void Hub::DrawMap(ui::Context& ui, const void* data, Vec2 min, Vec2 max)
{
	const Hub& hub = *static_cast<const MapView*>(data)->hub;
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::Account& account = hub.shown;
	render::PushClip(min, max);
	const f32 size = hub.HexSize();
	const u32 reach = std::max(account.deepest, 1u) + catalog.map.fog + 2;
	const f32 pulse = 0.5f + 0.5f * std::sin(ui.Time() * 4.0f);
	for (u32 ring = 0; ring <= reach; ++ring)
	{
		for (const sim::Hex hex : sim::RingHexes(ring))
		{
			const Vec2 center = hub.HexCenter(hex, min, max);
			if (center.x < min.x - size || center.x > max.x + size || center.y < min.y - size ||
			    center.y > max.y + size)
				continue;
			const bool visible = sim::IsVisible(catalog, account, hex);
			const bool cleared = sim::IsCleared(account, hex);
			const bool open = sim::CanAttack(account, hex);
			const sim::Node node = sim::NodeAt(catalog, hex);
			u32 fill = ui::Color(0x0a1220, 0.55f);
			if (ring == 0)
				fill = ui::Color(0x2a6fd6, 0.95f);
			else if (!visible)
				fill = ui::Color(0x0b0f18, 0.5f);
			else if (cleared)
				fill = ui::Color(0x1f7a4a, 0.9f);
			else if (open)
				fill = ui::Color(0x8a5a12, 0.9f);
			else
				fill = ui::Color(0x26344a, 0.85f);
			Vec2 corners[7];
			for (u32 k = 0; k < 6; ++k)
			{
				const f32 a = (60.0f * f32(k) - 30.0f) * 0.017453293f;
				corners[k] = {center.x + 0.94f * size * std::cos(a),
				              center.y + 0.94f * size * std::sin(a)};
			}
			corners[6] = corners[0];
			for (u32 k = 0; k < 6; ++k)
				render::AddTriangle(center, corners[k], corners[k + 1], fill, true);
			const bool picked = hex == hub.node;
			render::AddPolyline(corners, 7, picked ? 3.0f : 1.2f,
			                    picked ? ui::Color(0xffffff, 0.6f + 0.4f * pulse)
			                           : ui::Color(0x4a6a90, visible ? 0.8f : 0.25f));
			if (!visible || ring == 0)
				continue;
			// A rich node shows a gold dot; the ring's number in the middle.
			if (node.kind < catalog.map.kinds.size() && catalog.map.kinds[node.kind].loot > 1.0f)
				render::AddCircle({center.x + 0.45f * size, center.y - 0.45f * size}, 0.14f * size,
				                  GOLD);
			if (size >= 18.0f)
			{
				ui::TextLook look;
				look.size = 0.5f * size;
				look.color = ui::Color(0xe6f1ff, cleared ? 0.6f : 0.95f);
				const std::string number = std::to_string(ring);
				const Vec2 measured = ui.MeasureText(number.c_str(), look);
				ui.DrawText(number.c_str(), center - measured * 0.5f, look);
			}
		}
	}
	render::PopClip();
}

void Hub::BuildMap()
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	// A phone: the map over the node's panel.
	ui::Layout body = Row(compact ? 8.0f : 12.0f);
	body.align = ui::Align::Stretch;
	body.height = ui::Grow();
	if (compact)
		ui.BeginColumn(body);
	else
		ui.BeginRow(body);
	ui::Layout mapBox = Panel(ui::Grow(), ui::Grow());
	mapBox.padding = ui::All(4.0f);
	ui.BeginPanel(mapBox);
	const MapView view{this};
	ui.Custom("starmap", ui::Grow(), ui::Grow(), DrawMap, ui.CopyArray(&view, 1));
	ui.LastRect(ui.LastId(), mapMin, mapMax);
	if (!compact)
		ui.Text(strings.Get("hub.map_hint"), Note(theme));
	ui.End();
	BuildNode();
	ui.End();
}

void Hub::BuildNode()
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::Node info = sim::NodeAt(catalog, node);
	ui::Layout panel =
		Panel(compact ? ui::Grow() : ui::Fixed(340.0f), compact ? ui::Fit() : ui::Grow());
	if (compact)
	{
		panel.padding = ui::All(10.0f);
		panel.gap = 4.0f;
	}
	ui.BeginPanel(panel);
	const std::string kind = "node." + catalog.map.kinds[info.kind].id;
	const std::string heading =
		std::string(icon::Find("node_" + catalog.map.kinds[info.kind].id)) + " " +
		strings.Format("hub.node", Number(info.ring).c_str(), strings.Get(kind.c_str()));
	ui.Text(heading.c_str(), Title());
	const bool cleared = sim::IsCleared(shown, node);
	const bool open = sim::CanAttack(shown, node);
	ui.Text(strings.Get(cleared ? "hub.cleared"
	                    : open  ? "hub.open"
	                            : "hub.closed"),
	        Note(theme, cleared ? GOOD
	                    : open  ? GOLD
	                            : theme.textDim));
	ui.Separator();
	const std::string enemies = strings.Format("hub.enemies", Number(info.enemies).c_str());
	const std::string strength =
		strings.Format("hub.strength", Number(info.strength * 100.0f, "%.0f%%").c_str());
	const std::string loot =
		strings.Format("hub.loot", Number(info.loot * 100.0f, "%.0f%%").c_str());
	if (compact)
	{
		// One line on a phone: the map keeps the height.
		ui::TextLook line = Note(theme, theme.text);
		line.wrap = true;
		ui.Text((enemies + "   " + strength + "   " + loot).c_str(), line);
	}
	else
	{
		ui.Text(enemies.c_str());
		ui.Text(strength.c_str(), Note(theme));
		ui.Text(loot.c_str(), Note(theme));
	}
	if (!compact)
	{
		ui.Text(strings.Format("hub.deepest", Number(shown.deepest).c_str()).c_str(),
		        Note(theme, GOLD));
		ui.Text(strings
		            .Format("hub.record", Number(shown.stats.battles).c_str(),
		                    Number(shown.stats.won).c_str())
		            .c_str(),
		        Note(theme));
		ui.Text(strings
		            .Format("hub.losses", Number(shown.stats.kills).c_str(),
		                    Number(shown.stats.lost).c_str())
		            .c_str(),
		        Note(theme));
	}
	ui.Separator();
	ui.Text(strings.Get("hub.fleet"), Title());
	// Ships at home, up to the fleet's size.
	for (const sim::ShipRecord& record : shown.ships)
	{
		if (record.away)
			continue;
		ui.PushId(record.id);
		bool on = std::find(fleet.begin(), fleet.end(), record.id) != fleet.end();
		const std::string label = std::string(NameOf("hull", record.fit.hull)) + " #" +
		                          Number(record.id) + " " +
		                          Number(record.fit.condition * 100.0, "%.0f%%");
		if (ui.Toggle(label.c_str(), on))
		{
			if (on && fleet.size() < catalog.rules.maxFleet)
				fleet.push_back(record.id);
			else
				std::erase(fleet, record.id);
		}
		// A broken module or a battered hull: better mended first.
		const bool broken = std::any_of(record.fit.slots.begin(), record.fit.slots.end(),
		                                [](const sim::FittedModule& slot)
		                                { return !slot.id.empty() && slot.condition <= 0.0f; });
		if (broken || record.fit.condition < 0.5f)
			ui.Text(strings.Get(broken ? "hub.broken_module" : "hub.battered"), Note(theme, WARN));
		ui.PopId();
	}
	if (fleet.empty() && !shown.ships.empty())
	{
		for (const sim::ShipRecord& record : shown.ships)
		{
			if (!record.away)
			{
				fleet.push_back(record.id);
				break;
			}
		}
	}
	const sim::LaunchCost cost = sim::CostOfLaunch(catalog, node, u32(fleet.size()));
	sim::Resources fuel;
	fuel[sim::Resource::He3] = cost.fuel;
	ui.Text(
		strings.Format("hub.launch_cost", Number(cost.command).c_str(), Number(cost.fuel).c_str())
			.c_str(),
		Note(theme, CanPay(fuel) && shown.command >= cost.command ? theme.textDim : WARN));
	// Right under the cost, not at the panel's foot: on a phone held upright
	// the panel runs down to the screen's edge.
	if (launching)
		ui.Text(strings.Get("hub.launching"), Note(theme, GOLD));
	else if (open && !fleet.empty() && CanPay(fuel) && shown.command + 1e-6 >= cost.command &&
	         ui.Button(strings.Get("hub.launch"), ui::ButtonKind::Primary, ui::Grow()))
	{
		sim::Operation operation;
		operation.kind = sim::Operation::Kind::Launch;
		operation.node = node;
		operation.ships = fleet;
		Request(operation);
		launching = true;
	}
	ui.End();
}

void Hub::BuildReport()
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	ui.Spacer();
	ui::Layout middle = Row();
	middle.justify = ui::Justify::Center;
	ui.BeginRow(middle);
	ui::Layout panel = Panel(compact ? ui::Grow() : ui::Fixed(520.0f), ui::Fit());
	panel.padding = ui::All(compact ? 16.0f : 24.0f);
	panel.gap = 12.0f;
	ui.BeginPanel(panel);
	static constexpr const char* ENDS[] = {"report.cleared", "report.returned", "report.lost"};
	ui::TextLook title = Title();
	title.size = 30.0f;
	title.color = report.end == sim::BattleReport::End::Cleared ? GOOD
	              : report.end == sim::BattleReport::End::Lost  ? WARN
	                                                            : theme.text;
	ui.Text(strings.Get(ENDS[u32(report.end)]), title);
	ui.Text(strings.Format("report.node", Number(sim::RingOf(report.node)).c_str()).c_str(),
	        Note(theme));
	ui.Text(strings.Format("report.kills", Number(report.kills).c_str()).c_str());
	if (provider.empty())
		ui.Text(strings.Get("report.guest"), Note(theme, GOLD));
	if (report.lost > 0)
		ui.Text(strings.Format("report.ships_lost", Number(report.lost).c_str()).c_str(),
		        Note(theme, WARN));
	ui.Separator();
	ui.Text(strings.Get("report.loot"), Title());
	if (report.loot.empty())
		ui.Text(strings.Get("report.nothing"), Note(theme));
	for (const sim::Stack& stack : report.loot)
	{
		sim::Resource resource;
		std::string line;
		if (sim::FindResource(stack.id, resource))
			line = std::string(strings.Get(RESOURCE_KEYS[u32(resource)]));
		else if (sim::GetCatalog().FindModule(stack.id))
			line = NameOf("module", stack.id);
		else
			line = NameOf("ammo", stack.id);
		line += " x" + Number(stack.count);
		ui.Text(line.c_str());
	}
	ui.Spacer();
	if (ui.Button(strings.Get("report.continue"), ui::ButtonKind::Primary, ui::Grow()))
		reporting = false;
	ui.End();
	ui.End();
	ui.Spacer();
}
} // namespace sn
