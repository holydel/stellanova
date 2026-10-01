#pragma once

#include "ui.h"

#include <sn/sim/protocol.h>

#include <ph/audio/audio.h>
#include <ph/net/net.h>
#include <ph/os/event.h>

#include <memory>
#include <vector>

// Flying one ship by hand (roadmap M1.1), as a client of a server
// (docs/adr/0007-local-server.md): the controls go to it, its snapshots come
// back, and the ship is drawn between the last two, seen from above. Keys
// (W A S D, arrows), a gamepad (left stick, triggers), or a finger or the
// mouse held where the ship should go.
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
	bool Receive(ph::f32 dt);
	void Draw(ph::rhi::CommandList& commands, const ph::render::FrameTime& time, Ui& ui);

private:
	struct Rock
	{
		ph::Vec3 position;
		ph::Vec3 scale;
		ph::f32 spin = 0.0f;
		ph::u32 mesh = 0;
	};

	sim::ShipControls ReadControls(ph::os::PixelSize size) const;

	Resources* resources = nullptr;
	ph::net::Client client;
	ph::u32 ship = 0; // our ShipId, from Welcome
	// The last two snapshots, to draw what lies between.
	std::unique_ptr<sim::Snapshot> latest;
	std::unique_ptr<sim::Snapshot> previous;
	std::unique_ptr<sim::Snapshot> incoming; // read here first
	ph::f32 sinceSnapshot = 0.0f;
	std::vector<Rock> rocks;
	ph::audio::Voice engine;
};
} // namespace sn
