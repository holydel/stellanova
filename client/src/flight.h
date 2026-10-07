#pragma once

#include "connection.h"
#include "flight_effects.h"
#include "prediction.h"
#include "ui.h"

#include <sn/bots/orders.h>
#include <sn/sim/protocol.h>

#include <ph/audio/audio.h>
#include <ph/os/event.h>

#include <memory>
#include <string>
#include <vector>

// Flying a ship (roadmap M1.1) in an asteroid field (M1.3,
// docs/adr/0008-combat-v0.md) against enemy bots (0009-bots-v0.md), as a
// client of a server (docs/adr/0007-local-server.md): the skirmish, or a
// battle of the solo loop (docs/solo-loop.md). Orders move the ship
// (docs/adr/0013-ships-modules-damage.md): a click or a tap on space flies
// it there, on an enemy attacks it; keys, a pad's stick or a held finger
// steer directly. Its turrets aim and fire on their own, on the server. The
// controls go to the server a tick's worth at a time; snapshots, the field
// and events come back. Our ship flies at once from our controls
// (Prediction, docs/adr/0012-prediction-and-protocol-4.md); other ships are
// drawn between snapshots, a little behind the newest, seen from above.
// Enter (or the Chat button, for fingers) opens the chat.
namespace sn
{
struct Resources;

class Flight
{
public:
	enum class Action : ph::u8
	{
		None,
		Leave,  // back to the menu (the skirmish)
		Return, // the fleet home (a battle): the server answers with a Result
	};

	// On an open connection; `window` is for typing (the on-screen keyboard).
	void Enter(Resources& resources, Connection& connection, ph::os::WindowId window);
	void Leave();
	// While the chat is open, it takes every event.
	Action OnEvent(const ph::os::Event& event);
	// Our ticks in the frame's `dt`: each flies our ship and its controls go
	// to the server.
	void SendControls(ph::os::PixelSize size, ph::f32 dt);
	// A message of a match (Welcome, Snapshot, Events, Chat).
	void OnMessage(const ph::u8* data, ph::u32 size);
	// Whether the server ever welcomed us into a match.
	bool WasWelcomed() const { return welcomed; }
	bool IsBattle() const { return kind == sim::MatchKind::Battle; }
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);
	// The Network window, beside F1's diagnostics (Debug and Dev builds): the
	// connection's traffic, and how far behind the newest snapshot ships are
	// drawn (docs/profiling.md).
	void DrawNetworkWindow();

private:
	// A rock of the field: the server's circle, and looks of its own.
	struct Rock
	{
		ph::Vec2 position; // in the plane
		ph::f32 radius = 0.0f;
		bool broken = false;
		ph::u32 mesh = 0;
		ph::f32 height = 1.0f; // squashed or stretched up and down
		ph::f32 spin = 0.0f;   // rad/s around the vertical
		ph::f32 phase = 0.0f;
	};

	// Rocks far below the plane, only there to show speed.
	struct Scenery
	{
		ph::Vec3 position;
		ph::f32 size = 1.0f;
		ph::f32 spin = 0.0f;
		ph::u32 mesh = 0;
	};

	// Pieces of a broken rock or ship: the client's own.
	struct Piece
	{
		ph::Vec3 position;
		ph::Vec3 velocity;
		ph::f32 size = 1.0f;
		ph::f32 spin = 0.0f;
		ph::f32 life = 0.0f; // s left
		ph::f32 lifetime = 1.0f;
		ph::u32 mesh = 0;
		bool metal = false; // a ship's: a dark block, not a rock
	};

	// An enemy as the interface shows it: a bar over it, or a mark at the
	// screen's edge toward it; picked by a click.
	struct Mark
	{
		ph::u32 id = 0;
		ph::Vec2 position; // in the plane, as drawn
		ph::u8 health = 0;
		ph::u8 shield = 0;
	};

	// Loot floating in space, by its index in the server's pool.
	struct Crate
	{
		ph::u32 index = 0;
		ph::Vec2 position;
		ph::f32 age = 0.0f;
	};

	// A line of the chat, as shown.
	struct ChatLine
	{
		std::string text; // "name: text", or the server's notice
		bool notice = false;
		bool own = false;   // ours
		ph::f32 age = 0.0f; // s since it came
	};

	// From keys and pads: direct steering, when any is given.
	bool ReadSteering(sim::ShipControls& controls) const;
	// The pointer's orders: a press on an enemy attacks it, on space (or
	// held) flies there; the right button stops.
	void ReadPointer(ph::os::PixelSize size);
	// A point on the screen (pixels) in the plane.
	ph::Vec2 ToPlane(ph::os::PixelSize size, ph::f32 x, ph::f32 y) const;
	// This tick's controls: steering, else the order's.
	sim::ShipControls OrderControls() const;
	// Our newest controls not yet applied, a few, and our target, to the
	// server.
	void SendInput();
	// Our Steam name to the server, once there is one.
	void SendName();
	// A snapshot's word on our ship: the prediction corrected, the jump in
	// what is drawn smoothed away.
	void Correct(const sim::ShipState& own);
	// Our predicted ship where it is drawn now: between the last two ticks,
	// plus what is left of a correction.
	void OwnPose(ph::Vec2& position, ph::f32& angle) const;
	void OnWelcome(const sim::Welcome& welcome);
	void OnEvents(const sim::Events& events);
	void OnChat(const sim::Chat& chat);
	// The chat's line being typed: open, keys and text, closed.
	void StartTyping();
	void StopTyping();
	void OnTyping(const ph::os::Event& event);
	// The last lines and the one being typed, above the on-screen keyboard;
	// for fingers, the button that opens it.
	void DrawChat(Ui& ui, ph::f32 dt);
	// A new snapshot: ships that arrived warp in; a new wave shows.
	void OnSnapshot();
	void Burst(ph::Vec2 at, ph::u32 count, ph::f32 size, ph::f32 speed, bool metal);
	// A sound from `at` in the plane: quieter away from the camera, and to
	// its side.
	void PlayAt(ph::audio::Sound sound, ph::f32 volume, ph::Vec2 at) const;
	ph::u8 TeamOf(ph::u32 id) const;
	void DrawHud(Ui& ui, const sim::ShipState& own, ph::f32 dt);
	// Our ship's modules, capacitor, ammo and hold.
	void DrawSystems(Ui& ui, const sim::ShipState& own, ph::f32 top);
	// A button of the HUD: true when `rect` (pixels) holds the point.
	static bool Inside(const ph::f32 (&rect)[4], ph::f32 x, ph::f32 y);
	// Until the server's first snapshot: to where it connects.
	void DrawConnecting(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);
	// Lays out every text the HUD shows, in its looks, once at the start:
	// a font's first use costs milliseconds (10 ms on the web), which the
	// first wave's banner would otherwise show as a hitch.
	void WarmHud(Ui& ui);

	Resources* resources = nullptr;
	Connection* connection = nullptr;
	ph::u32 ship = 0;           // our ShipId, from Welcome
	ph::u8 team = sim::PLAYERS; // our ship's
	ph::f32 respawn = 3.0f;     // s from our wreck to our ship's return (the skirmish)
	sim::MatchKind kind = sim::MatchKind::Skirmish;
	ph::u32 ring = 0;                 // a battle's
	std::vector<std::string> modules; // our slots' catalog ids
	ph::f32 attackDistance = 40.0f;   // m: where an attack circles its target
	// The last snapshots, oldest first: ships are drawn between two of them,
	// a little behind the newest, more so online, where snapshots come
	// unevenly (renderTick, in ticks).
	static constexpr ph::u32 HISTORY = 8;
	std::vector<std::unique_ptr<sim::Snapshot>> history;
	std::unique_ptr<sim::Snapshot> incoming; // read here first
	const sim::Snapshot* latest = nullptr;   // the newest
	ph::f64 renderTick = 0.0;
	ph::f32 delayTicks = 1.0f; // 1 on this machine, 3 online
	bool welcomed = false;
	std::vector<Rock> rocks;
	std::vector<sim::Rock> field; // the same, as the sim has them: for orders and beams
	std::vector<Scenery> scenery;
	std::vector<Piece> pieces;
	std::vector<Crate> crates;
	std::vector<ph::u32> present;  // ships flying in the latest snapshot
	std::vector<Mark> marks;       // this frame's enemies
	std::vector<ph::Vec2> friends; // this frame's ships of our side, as drawn
	// Our ship's first turret, as drawn this frame: the HUD shows its aim.
	bool turretShown = false;
	ph::Vec2 turretAt;
	ph::f32 turretAim = 0.0f; // rad, as ship angles go
	bool turretFiring = false;
	FlightEffects effects; // flame, trails, shields, sparks, blasts
	ShipLook ownLook;
	ShipLook enemyLook;
	ph::Vec2 listener;        // where the camera looks, in the plane
	ph::Vec3 eye;             // where the camera is
	ph::f32 height = 0.0f;    // the camera's, above the plane: m
	ph::f32 zoom = 1.0f;      // the wheel's: the camera's height, times this
	ph::u32 kills = 0;        // enemies we destroyed
	ph::u32 wave = 0;         // the enemies' wave
	ph::f32 waveShown = 0.0f; // s since the wave's number came up
	ph::f32 flown = 0.0f;     // s since the flight began
	ph::f32 cleared = -1.0f;  // s since the battle's area was cleared; negative before
	bool warmed = false;      // the HUD's texts laid out once (WarmHud)
	ph::f32 downFor = -1.0f;  // s since our ship broke apart; negative while it flies
	ph::f32 hurt = 0.0f;      // the red edges after a hull hit: 0 to 1
	ph::f32 shake = 0.0f;     // the camera's, after a bump: 0 to 1
	ph::u8 lastHealth = 0;    // ours in the last snapshot: beams tell no events
	ph::u32 random = 1;       // for the looks of effects
	ph::audio::Voice engine;
	// Orders: what the ship does when nobody steers.
	bots::Order order;
	ph::u32 target = 0;        // the enemy our turrets prefer: a ShipId, 0 for none
	bool pressed = false;      // the pointer's button was down last frame
	bool following = false;    // a held pointer moves the order's point
	bool steering = false;     // keys or a stick steer this tick
	ph::f32 orderShown = 0.0f; // s since the last order: its mark fades
	// Our ticks and their controls, and the prediction.
	Prediction prediction;
	bool autopiloted = false; // the server flies our ship: nothing to predict
	ph::f32 tickTime = 0.0f;  // s into our next tick
	ph::u64 ticks = 0;        // our ticks since Welcome
	ph::Vec2 smoothing;       // a correction's jump, fading out of what is drawn
	ph::f32 smoothingAngle = 0.0f;
	bool named = false;         // our Steam name went to the server
	ph::u32 staleSnapshots = 0; // late or doubled, dropped
	// The chat.
	ph::os::WindowId window = 0;
	std::vector<ChatLine> chat; // oldest first
	bool typing = false;
	std::string draft;             // what is typed, UTF-8
	bool sawTouch = PH_OS_ANDROID; // a finger touched: the Chat button shows
	ph::f32 chatButton[4] = {};    // its rectangle in pixels: x, y, width, height
	ph::f32 returnButton[4] = {};  // a battle's Return, the same way
};
} // namespace sn
