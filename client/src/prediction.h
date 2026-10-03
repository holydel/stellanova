#pragma once

#include <sn/sim/protocol.h>

#include <vector>

// Our own ship, flown at once from our own controls (docs/adr/0003-netcode-model.md,
// 0012-prediction-and-protocol-4.md). Each tick of ours it flies by the
// controls we send, with the sim's own code. Each snapshot brings the
// server's word on it, after the last of our controls the server applied;
// the ship starts again from there and flies the controls the server has
// not applied yet. Other ships are not predicted: bumping into them is the
// server's to tell.
namespace sn
{
class Prediction
{
public:
	// Controls kept until the server applies them: two seconds of ticks.
	static constexpr ph::u32 KEPT = 64;

	// A tick's controls, numbered from 1 (sim::Input).
	struct Controls
	{
		ph::u32 number = 0;
		sim::ShipControls controls;
	};

	// A new ship (Welcome): how it flies and fires, and the field.
	void Reset(const sim::HullClass& hull, const sim::WeaponClass& weapon,
	           std::vector<sim::Rock> rocks);
	// A rock broke (an event): ships fly through where it was.
	void BreakRock(ph::u32 rock);
	// Our next tick's controls, as the server will apply them
	// (sim::Quantize): kept for the server, and flown at once while the ship
	// is predicted. True when the gun fires, with the shot the server makes.
	bool Step(const sim::ShipControls& controls, sim::Shot& shot);
	// The server's word on our ship and its gun's cooldown, after it applied
	// our controls up to number `applied`. A wreck is not predicted until it
	// is back.
	void Correct(const sim::ShipState& state, ph::f32 cooldown, ph::u32 applied);

	bool IsActive() const { return active; }
	// Our ship now, and a tick before: drawn between the two.
	const sim::Ship& Now() const { return now; }
	const sim::Ship& Before() const { return before; }
	// The controls the server has not applied yet, oldest first.
	const std::vector<Controls>& Pending() const { return pending; }

private:
	// One tick of our ship, as sim::Step flies it, without other ships.
	bool Fly(const sim::ShipControls& controls, sim::Shot& shot);

	sim::Ship now;
	sim::Ship before;
	std::vector<sim::Rock> rocks;
	std::vector<Controls> pending;
	ph::u32 number = 0; // the newest controls'
	bool active = false;
};
} // namespace sn
