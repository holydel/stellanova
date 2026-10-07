#pragma once

#include <sn/sim/protocol.h>

#include <ph/net/net.h>

#include <deque>
#include <string>
#include <vector>

// A connection to a game server (docs/adr/0007-local-server.md,
// 0014-accounts-on-the-game-server.md): it says Hello for the skirmish or
// the hub once connected, counts every message, and can make the network
// worse on purpose (--lag, --loss). The skirmish's flight, the hub and its
// battles read what comes through it; a Refusal ends it.
namespace sn
{
class Connection
{
public:
	void Open(const char* address, sim::Joining joining);
	void Close();
	bool IsOpen() const { return bool(client); }
	const std::string& Address() const { return address; }
	// A server in this process: its snapshots come every tick.
	bool IsLocal() const { return address.rfind("loopback:", 0) == 0; }
	// The address without its scheme, port and path: "wos-observer.com".
	std::string HostName() const;

	// Messages that came and are due, each to `handle` (with `self`). False
	// when the server is gone or turned us away.
	using Handler = void (*)(void* self, const ph::u8* data, ph::u32 size);
	bool Receive(Handler handle, void* self);
	// To the server: counted, and through the network simulator if it is on.
	// What is posted before Hello went out waits for it (a Login).
	void Post(const std::vector<ph::u8>& bytes, ph::net::Delivery delivery);

	// Whether the server ever answered (a Welcome or a Profile).
	bool WasAnswered() const { return answered; }
	void SetAnswered() { answered = true; }
	// Whether, and why, the server turned us away.
	bool WasRefused(sim::RefusalReason& reason) const
	{
		reason = refusal;
		return refused;
	}

	// A worse network than the real one, for trying prediction (--lag,
	// --loss): each message comes half the round trip late, and snapshots and
	// inputs are lost at the rate given (0 to 1). Kept across connections.
	void SimulateNetwork(ph::f32 roundTrip, ph::f32 loss);
	// The Network window's traffic part, beside F1's diagnostics (Debug and
	// Dev builds); `staleSnapshots` from the flight.
	void DrawNetworkWindow(ph::u32 staleSnapshots);

private:
	// A message held back by the network simulator until it is due.
	struct Held
	{
		ph::u64 dueNs = 0;
		std::vector<ph::u8> bytes;
		ph::net::Delivery delivery = ph::net::Delivery::Reliable;
	};

	ph::f32 Random();

	ph::net::Client client;
	std::string address;
	sim::Joining joining = sim::Joining::Skirmish;
	bool greeted = false;                   // Hello went out
	std::vector<std::vector<ph::u8>> early; // posted before it
	bool answered = false;
	bool refused = false;
	sim::RefusalReason refusal = sim::RefusalReason::Version;
	ph::u64 lagNs = 0; // each way
	ph::f32 loss = 0.0f;
	ph::u32 random = 1;
	std::deque<Held> outbox;
	std::deque<Held> inbox;

	// Diagnostics: messages by type, and what the Network window read last
	// (twice a second) with the rates since the one before.
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
	NetView netView;
};
} // namespace sn
