#pragma once

#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <memory>
#include <vector>

// A match's authority (docs/adr/0007-local-server.md): it listens at an
// address, gives each client that says Hello a ship, applies their inputs,
// runs the sim at its fixed tick, and sends every client a snapshot after
// each tick. No globals: a client can host one in its own process.
namespace sn::server
{
class Server
{
public:
	// "loopback:<name>" for a local game. False when the address is taken.
	bool Start(const char* address);
	void Stop();
	bool IsRunning() const { return bool(listener); }
	// What the clients said, then the ticks that `dt` seconds cover.
	void Update(ph::f32 dt);
	const sim::World* GetWorld() const { return world.get(); }

private:
	struct Player
	{
		ph::net::PeerId peer = 0;
		sim::ShipHandle ship;
	};

	void OnMessage(ph::net::PeerId peer, const ph::u8* data, ph::u32 size);
	void Leave(ph::net::PeerId peer);
	void SendSnapshots();

	ph::net::Server listener;
	std::unique_ptr<sim::World> world;
	std::vector<Player> players;
	ph::f32 sinceTick = 0.0f;
	sim::Snapshot snapshot; // reused each tick
};
} // namespace sn::server
