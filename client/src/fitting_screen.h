#pragma once

#include "inventory_items.h"

#include <ph/ui/ui.h>

#include <string>
#include <vector>

// The fitting screen (docs/adr/0017-inventory.md), the hub's Hangar tab, as
// EVE Online's fitting window: the ships and the stored modules that fit the
// picked one on the left; the ship in 3D in the middle (ShipView, drawn by
// the hub) inside a ring of its slots, externals over it, internals on its
// right, rigs under it; its numbers on the right. A module dragged onto a
// slot is fitted there, one dragged from a slot back to the modules goes to
// storage; what that asks goes to the server.
namespace sn
{
struct Resources;

class FittingScreen
{
public:
	struct Asks
	{
		std::vector<sim::Operation> requests;
		std::string notice; // a strings key
	};

	// `compact` on a phone held upright: the ships as chips, the ring, and
	// under it the modules or the numbers.
	void Build(ph::ui::Context& ui, Resources& resources, const sim::Account& shown, Asks& asks,
	           bool compact = false);
	// The ring's middle from the last layout (units), where the hub draws the
	// ship: false when there is none to show.
	bool ShipRegion(ph::Vec2& min, ph::Vec2& max) const;
	ph::u32 Ship() const { return ship; }
	void PickShip(ph::u32 id) { ship = id; }

private:
	void BuildShips(ph::ui::Context& ui, Asks& asks);
	void BuildModules(ph::ui::Context& ui, Asks& asks);
	void BuildCompact(ph::ui::Context& ui, Asks& asks);
	void BuildRing(ph::ui::Context& ui, const sim::ShipRecord& record, Asks& asks);
	void BuildStats(ph::ui::Context& ui, const sim::ShipRecord& record);
	void BuildNumbers(ph::ui::Context& ui, const sim::ShipRecord& record);
	void Drop(const void* payload, Place onto, ph::i32 slot, Asks& asks);

	Resources* resources = nullptr;
	const sim::Account* shown = nullptr;
	ph::u32 ship = 0;
	std::string picked; // a socket's or a module's key
	ph::Vec2 ringMin;   // the ring's region, units (last layout)
	ph::Vec2 ringMax;
	bool ringShown = false;
	bool compact = false;
	bool numbers = false; // compact: the numbers under the ring, not the modules
};
} // namespace sn
