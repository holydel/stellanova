#pragma once

#include "flight_effects.h"
#include "ui.h"

#include <sn/sim/protocol.h>

#include <ph/audio/audio.h>
#include <ph/net/net.h>
#include <ph/os/event.h>

#include <memory>
#include <string>
#include <vector>

// Flying one ship by hand (roadmap M1.1) in an asteroid field (M1.3,
// docs/adr/0008-combat-v0.md) against waves of enemy bots (0009-bots-v0.md),
// as a client of a server (docs/adr/0007-local-server.md): the controls go
// to it; its snapshots, the field and the events come back. Ships are drawn
// between the last two snapshots, seen from above; the camera rises while
// enemies are near. A pad: the left trigger thrusts, the left bumper
// reverses, the left stick turns, the right trigger fires. Keys: W A S D,
// Space fires. A finger (or the mouse) held where the ship should go;
// another finger (or the right button) fires.
namespace sn
{
struct Resources;

class Flight
{
public:
	// Connects to the server at `address`.
	void Enter(Resources& resources, const char* address);
	void Leave();
	// True: back to the menu.
	bool OnEvent(const ph::os::Event& event);
	// The controls to the server; then, after the server's update in a
	// local game, Receive takes its answers. False when the server is gone.
	void SendControls(ph::os::PixelSize size);
	bool Receive();
	// Whether the server ever welcomed us: else it never answered.
	bool WasWelcomed() const { return welcomed; }
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);
	// The Network window, beside F1's diagnostics (Debug and Dev builds): the
	// connection's traffic, its messages by type, and how far behind the
	// newest snapshot ships are drawn (docs/profiling.md).
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
	// screen's edge toward it.
	struct Mark
	{
		ph::Vec2 position; // in the plane
		ph::u8 health = 0;
		ph::u8 shield = 0;
	};

	sim::ShipControls ReadControls(ph::os::PixelSize size) const;
	void OnWelcome(const sim::Welcome& welcome);
	void OnEvents(const sim::Events& events);
	// A new snapshot: ships that arrived warp in; a new wave shows.
	void OnSnapshot();
	void Burst(ph::Vec2 at, ph::u32 count, ph::f32 size, ph::f32 speed, bool metal);
	// A sound from `at` in the plane: quieter away from the camera, and to
	// its side.
	void PlayAt(ph::audio::Sound sound, ph::f32 volume, ph::Vec2 at) const;
	ph::u8 TeamOf(ph::u32 id) const;
	void DrawHud(Ui& ui, const sim::ShipState& own, ph::f32 dt);
	// Until the server's first snapshot: to where it connects.
	void DrawConnecting(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);
	// Lays out every text the HUD shows, in its looks, once at the start:
	// a font's first use costs milliseconds (10 ms on the web), which the
	// first wave's banner would otherwise show as a hitch.
	void WarmHud(Ui& ui);

	Resources* resources = nullptr;
	ph::net::Client client;
	ph::u32 ship = 0;           // our ShipId, from Welcome
	ph::u8 team = sim::PLAYERS; // our ship's
	ph::f32 respawn = 3.0f;     // s from our wreck to our ship's return
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
	std::string server; // where it connected, for "Connecting to".
	std::vector<Rock> rocks;
	std::vector<Scenery> scenery;
	std::vector<Piece> pieces;
	std::vector<ph::u32> present; // ships flying in the latest snapshot
	std::vector<Mark> marks;      // this frame's enemies
	FlightEffects effects;        // flame, trails, shields, sparks, blasts
	ShipLook ownLook;
	ShipLook enemyLook;
	ph::Vec2 listener;        // where the camera looks, in the plane
	ph::Vec3 eye;             // where the camera is
	ph::f32 height = 0.0f;    // the camera's, above the plane: m
	ph::u32 kills = 0;        // enemies we destroyed
	ph::u32 wave = 0;         // the enemies' wave
	ph::f32 waveShown = 0.0f; // s since the wave's number came up
	ph::f32 flown = 0.0f;     // s since the flight began
	bool warmed = false;      // the HUD's texts laid out once (WarmHud)
	ph::f32 downFor = -1.0f;  // s since our ship broke apart; negative while it flies
	ph::f32 hurt = 0.0f;      // the red edges after a hull hit: 0 to 1
	ph::f32 shake = 0.0f;     // the camera's, after a bump: 0 to 1
	ph::u32 random = 1;       // for the looks of effects
	ph::audio::Voice engine;
	ph::u64 inputSentNs = 0; // when our controls last went

	// Diagnostics: messages by type, snapshots dropped, and what the Network
	// window read last (twice a second) with the rates since the one before.
	struct NetView
	{
		static constexpr ph::u32 HISTORY = 120; // a minute of readings
		ph::u64 atNs = 0;
		ph::net::TrafficStats traffic;
		sim::MessageCounts sent;
		sim::MessageCounts received;
		ph::net::TrafficRates rates;
		ph::f64 sentPerSecond[sim::MessageCounts::TYPES] = {};
		ph::f64 receivedPerSecond[sim::MessageCounts::TYPES] = {};
		ph::f32 receivedKb[HISTORY] = {}; // KB/s in, oldest at `next`
		ph::u32 next = 0;
	};
	sim::MessageCounts sentCounts;
	sim::MessageCounts receivedCounts;
	ph::u32 staleSnapshots = 0; // late or doubled, dropped
	NetView netView;
};
} // namespace sn
