#pragma once

#include "inventory_items.h"

#include <ph/ui/ui.h>

#include <string>
#include <vector>

// The inventory (docs/adr/0017-inventory.md), the hub's Storage tab, as EVE
// Online's cargo window: the account's places on the left (the colony's
// storage, each ship with its hold and its fitting, the blueprints), the
// picked place's items as tiles on the right, in a scrolling grid. Items move
// by dragging them onto another place, a ship, or one of a fitting's slots;
// what that asks goes to the server, which answers with the account (or a
// notice saying why not).
namespace sn
{
struct Resources;

// What the inventory's drags carry (a Carried), for every screen that takes
// them.
constexpr ph::u32 DRAG_ITEM = 0x534e4931;
constexpr ph::f32 TILE_WIDTH = 82.0f;
constexpr ph::f32 TILE_HEIGHT = 104.0f;

// An item's name from the strings ("Pulse laser S", "Empty external slot").
std::string ItemName(Resources& resources, const Tile& tile);
// An item's tile, as the inventory's grid shows it: its picture (or its
// glyph), its count, its name, a worn module's bar; outlined when picked.
// It is dragged when it can move (Carry from `from`), and tells what it is
// on hover.
ph::ui::ItemState ItemTile(ph::ui::Context& ui, Resources& resources, const Tile& tile, Place from,
                           bool picked);
// A tile's key: what it is, so that a pick survives the account's updates
// while the thing is there.
std::string TileKey(const Tile& tile);

class Inventory
{
public:
	// What the player asked this frame.
	struct Asks
	{
		std::vector<sim::Operation> requests;
		std::string notice; // a strings key: why a drop does nothing
	};

	// Inside the hub's frame, filling the space it is given; `compact` on a
	// phone held upright: the places as chips over the grid.
	void Build(ph::ui::Context& ui, Resources& resources, const sim::Account& shown, Asks& asks,
	           bool compact = false);
	void Show(Place where) { place = where; }

private:
	// A row of the places' tree: picks its place when clicked, takes drops.
	void PlaceRow(ph::ui::Context& ui, const char* id, const std::string& label,
	              const std::string& note, Place where, bool indented, Asks& asks);
	// The picked place's items: its name and room, the grid, the pick.
	void BuildItems(ph::ui::Context& ui, Asks& asks);
	void BuildCompact(ph::ui::Context& ui, Asks& asks);
	// A place as a chip (compact): picks it when tapped, takes drops.
	void PlaceChip(ph::ui::Context& ui, const char* id, const std::string& label, Place where,
	               bool on, Asks& asks, bool grow = false);
	void BuildTiles(ph::ui::Context& ui, Asks& asks);
	void BuildPicked(ph::ui::Context& ui, Asks& asks);
	// The drop of what ph::ui carried onto `onto` (a slot's number, or -1).
	void Drop(const void* payload, Place onto, ph::i32 slot, Asks& asks);
	std::string PlaceName(Place where) const;

	Resources* resources = nullptr;
	const sim::Account* shown = nullptr;
	Place place;
	std::vector<Tile> tiles; // the place's, this frame
	std::string picked;      // the picked tile's key (TileKey)
	bool mobile = false;     // Build's `compact`: fewer words (no hint)
};
} // namespace sn
