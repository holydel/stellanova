#pragma once

#include "colony_view.h"
#include "connection.h"
#include "fitting_screen.h"
#include "inventory.h"
#include "ship_view.h"

#include <sn/sim/account.h>

#include <ph/os/event.h>
#include <ph/os/window.h>
#include <ph/render/frame.h>
#include <ph/ui/ui.h>

#include <string>
#include <vector>

// The solo loop's screens (docs/solo-loop.md, docs/adr/0014-accounts-on-the-game-server.md,
// 0016-sign-in-and-admin.md), on pith's interface (ph::ui): the colony, the
// hangar, storage and the star map, over the account the server keeps. It
// logs in with the device's key, and with a Steam ticket where Steam runs:
// a signed-in account is saved, a guest's lasts while connected, and the
// top bar says which. Each request goes to the server,
// which answers with the account as it is then (or a notice saying why
// not); between answers, production counts on here by the same rules. A
// launch starts a battle: the server's Welcome takes the game to its flight,
// and the battle's Result comes back here as a report.
namespace sn
{
struct Resources;

class Hub
{
public:
	enum class Action : ph::u8
	{
		None,
		Leave, // back to the menu
	};

	// On a connection opened for the hub; it logs in with the device's key
	// (and Steam's ticket, once Steam answers).
	void Enter(Resources& resources, Connection& connection);
	void Leave();
	Action OnEvent(const ph::os::Event& event);
	// Signed, Profile, Result, and the server's notices.
	void OnMessage(const ph::u8* data, ph::u32 size);
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time);
	// Pixels per DPI-independent unit of the window's display
	// (os::GetWindowScale): on a phone, units are never smaller than its dp.
	void SetDensity(ph::f32 pixelsPerUnit) { density = pixelsPerUnit; }
	// The window's edges the system covers (os::GetSafeInsets, pixels): the
	// screens keep clear of a phone's bars.
	void SetInsets(ph::os::SafeInsets pixels) { insets = pixels; }
	// The mobile style (Settings::ResolvedStyle) and the interface's size
	// over the screen's (Settings::interfaceSize).
	void SetLook(bool mobile, ph::f32 interfaceSize)
	{
		mobileStyle = mobile;
		sizeFactor = interfaceSize;
	}
	// "colony", "hangar", "storage" (or "inventory") or "map" (--tab, for
	// screenshots).
	void ShowTab(const char* name);
	// Once the account comes: a launch of its first ship at home (--battle,
	// for screenshots and smoke tests).
	void LaunchOnce() { autoLaunch = true; }
	// Whether the server ever sent the account.
	bool HasAccount() const { return have; }
	// The Leave button was pressed (a click, a tap): once.
	bool TakeLeave()
	{
		const bool was = leaving;
		leaving = false;
		return was;
	}
	// This device's account key: made on its first run, kept since
	// (os::WriteSavedFile; the browser's storage on the web). The server
	// may hand out another at sign-in, which replaces it.
	static std::string DeviceKey();
	static void KeepKey(const std::string& key);

private:
	enum class Tab : ph::u8
	{
		Colony,
		Hangar,
		Storage,
		Map,
	};

	// What the map's drawing needs for a frame.
	struct MapView
	{
		const Hub* hub = nullptr;
	};

	// How far the Login is: Steam's turn (the platform, then its ticket),
	// sent, or sent as a guest while Steam may still come.
	enum class LoginState : ph::u8
	{
		Platform,
		Ticket,
		Sent,
		Guest,
		LateTicket,
	};

	// The Login when it is due: Steam's ticket, or none after a while.
	void UpdateLogin();
	void SendLogin(const std::string& ticket);
	void Request(const sim::Operation& operation);
	// The server's clock now, from its last word.
	ph::i64 Now() const;
	void Build();
	void BuildTop();
	// The tabs: buttons in the top bar, or on a phone a bar at the bottom.
	void TabButtons();
	void BuildColony();
	void BuildCard(ph::u32 building);
	void BuildHangar();
	void BuildStorage();
	void BuildMap();
	void BuildNode();
	void BuildReport();
	void BuildWaiting();
	// A line of a cost: each resource it takes; red where the colony lacks it.
	void CostLine(const sim::Resources& cost);
	bool CanPay(const sim::Resources& cost) const;
	// "Lancer", "Pulse laser S": an id's name from the strings, after its
	// icon when it has one (icon_codes.h).
	std::string NameOf(const char* prefix, const std::string& id);
	// The map: hexes around the colony, by their state; a click picks one.
	static void DrawMap(ph::ui::Context& ui, const void* data, ph::Vec2 min, ph::Vec2 max);
	// Over the colony's scene: the name and level of the building hovered or
	// picked, and of each one being upgraded (with its time left).
	static void DrawColonyLabels(ph::ui::Context& ui, const void* data, ph::Vec2 min, ph::Vec2 max);
	// Whether the Colony tab shows its scene now.
	bool ShowsColony() const { return have && tab == Tab::Colony && !reporting; }
	ph::Vec2 HexCenter(sim::Hex hex, ph::Vec2 min, ph::Vec2 max) const;
	sim::Hex HexAt(ph::Vec2 point) const;
	ph::f32 HexSize() const { return 26.0f * zoom; }

	Resources* resources = nullptr;
	Connection* connection = nullptr;
	ph::ui::Context ui;
	bool themed = false;
	LoginState login = LoginState::Platform;
	ph::f64 loginWait = 0.0; // s in this state
	std::string provider;    // the account's sign-in, as Signed said; "" for a guest
	bool told = false;       // Signed came
	sim::Account account;    // the server's word
	sim::Account shown;      // the same, counted on to now
	bool have = false;
	ph::i64 serverNow = 0;    // the server's clock when its word came...
	ph::f64 receivedAt = 0.0; // ...at this monotonic time, s
	Tab tab = Tab::Colony;
	sim::Hex node{1, 0};
	std::vector<ph::u32> fleet; // the ships picked to launch
	// The map's view, in units: where its middle is, how far in.
	ph::Vec2 pan;
	ph::f32 zoom = 1.0f;
	ph::Vec2 mapMin;
	ph::Vec2 mapMax;
	bool mapPressed = false;
	bool mapDragged = false;
	ph::Vec2 pressAt;
	ph::Vec2 panAt;
	ph::f32 unitsPerPixel = 1.0f;
	ph::f32 density = 1.0f; // SetDensity
	ph::os::SafeInsets insets;
	bool compact = false;     // the mobile style's layout
	bool mobileStyle = false; // SetLook
	ph::f32 sizeFactor = 1.0f;
	ColonyView colony;
	Inventory inventory;   // the Storage tab
	FittingScreen fitting; // the Hangar tab
	ShipView shipView;     // its ship
	ph::Vec2 sceneMin;     // the scene's part of the screen, in units
	ph::Vec2 sceneMax;
	ph::i32 hovered = -1; // the building under the pointer
	ph::i32 picked = -1;  // the building clicked: its card is marked
	std::string notice;   // the last refusal's strings key
	ph::f32 noticeLeft = 0.0f;
	bool reporting = false;
	sim::BattleReport report;
	bool launching = false; // waiting for a battle's Welcome
	bool leaving = false;
	bool autoLaunch = false;
};
} // namespace sn
