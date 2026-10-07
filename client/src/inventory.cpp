#include "inventory.h"

#include "icon_codes.h"
#include "resources.h"

#include <sn/sim/catalog.h>
#include <sn/sim/fitting.h>

#include <ph/render/shapes.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sn
{
namespace
{
using namespace ph;

constexpr f32 PLACES_WIDTH = 250.0f;
constexpr f32 PICTURE = 64.0f;

const u32 WARN = ui::Color(0xff6b5b);
const u32 GOOD = ui::Color(0x5ee38a);
const u32 GOLD = ui::Color(0xffc857);

ui::TextLook Small(const ui::Theme& theme, u32 color = 0)
{
	ui::TextLook look;
	look.size = theme.smallSize;
	look.color = color ? color : theme.textDim;
	return look;
}

std::string Number(f64 value)
{
	char text[32];
	std::snprintf(text, sizeof(text), "%.0f", value);
	return text;
}

std::string Volume(f64 value)
{
	char text[32];
	std::snprintf(text, sizeof(text), value < 10.0 ? "%.2f" : "%.1f", value);
	return text;
}

const sim::ShipRecord* FindShip(const sim::Account& account, u32 id)
{
	for (const sim::ShipRecord& ship : account.ships)
	{
		if (ship.id == id)
			return &ship;
	}
	return nullptr;
}

// What a tile's drawing needs, copied for the frame.
struct TileLook
{
	rhi::Texture picture; // the art pack's "icon.<id>"; none: the glyph
	const char* glyph = "";
	const char* name = "";
	const char* count = ""; // "" for none
	f32 condition = 1.0f;   // under 1: a bar
	bool empty = false;     // an empty slot
};

// The name in at most two centered lines of `width`, split at a space.
void DrawName(ui::Context& ui, const char* name, Vec2 top, f32 width, const ui::TextLook& look)
{
	const auto line = [&](const std::string& text, f32 y)
	{
		const Vec2 size = ui.MeasureText(text.c_str(), look);
		ui.DrawText(text.c_str(), {top.x + 0.5f * (width - std::min(size.x, width)), y}, look);
	};
	const std::string text = name;
	// A little slack: names that just fit stay on one line in every tile.
	const Vec2 whole = ui.MeasureText(name, look);
	if (whole.x <= width + 2.0f)
	{
		line(text, top.y);
		return;
	}
	usize split = std::string::npos;
	for (usize space = text.find(' '); space != std::string::npos;
	     space = text.find(' ', space + 1))
	{
		if (ui.MeasureText(text.substr(0, space).c_str(), look).x <= width)
			split = space;
	}
	if (split == std::string::npos)
	{
		line(text, top.y);
		return;
	}
	line(text.substr(0, split), top.y);
	line(text.substr(split + 1), top.y + whole.y);
}

void DrawTile(ui::Context& ui, const void* data, Vec2 min, Vec2 max)
{
	const TileLook& look = *static_cast<const TileLook*>(data);
	const ui::Theme& theme = ui.GetTheme();
	const f32 width = max.x - min.x;
	const Vec2 at = {min.x + 0.5f * (width - PICTURE), min.y};
	const Vec2 to = {at.x + PICTURE, at.y + PICTURE};
	if (look.empty)
		render::AddBox(at, to,
		               {.fill = ui::Color(0x0b111c, 0.55f),
		                .radius = 3.0f,
		                .border = theme.border,
		                .borderColor = ui::Color(0x3e4d6b, 0.9f)});
	else if (look.picture)
		render::AddImage(at, to, look.picture, 0xffffffff, {0.0f, 0.0f, 1.0f, 1.0f}, 3.0f);
	else
		render::AddBox(
			at, to,
			{.fill = ui::Color(0x111a2a), .fillBottom = ui::Color(0x0a0f16), .radius = 3.0f});
	if ((look.empty || !look.picture) && look.glyph[0])
	{
		ui::TextLook glyph;
		glyph.size = look.empty ? 22.0f : 30.0f;
		glyph.color = look.empty ? ui::Color(0x3e4d6b) : ui::Color(0x97999f);
		const Vec2 size = ui.MeasureText(look.glyph, glyph);
		ui.DrawText(look.glyph,
		            {at.x + 0.5f * (PICTURE - size.x), at.y + 0.5f * (PICTURE - size.y)}, glyph);
	}
	// The count on the picture's corner, as EVE's.
	if (look.count[0])
	{
		ui::TextLook text;
		text.size = 11.0f;
		text.color = ui::Color(0xe8eef8);
		const Vec2 size = ui.MeasureText(look.count, text);
		const Vec2 high = {to.x - 2.0f, to.y - 2.0f};
		const Vec2 low = {high.x - size.x - 6.0f, high.y - size.y - 1.0f};
		render::AddBox(low, high, {.fill = ui::Color(0x05080e, 0.82f), .radius = 2.0f});
		ui.DrawText(look.count, {low.x + 3.0f, low.y + 0.5f}, text);
	}
	// A worn module's condition along the picture's bottom.
	if (!look.empty && look.condition < 0.999f)
	{
		const f32 share = std::clamp(look.condition, 0.0f, 1.0f);
		const u32 color = share > 0.6f ? GOOD : share > 0.3f ? GOLD : WARN;
		const Vec2 low = {at.x + 3.0f, at.y + 3.0f};
		render::AddBox(low, {to.x - 3.0f, low.y + 3.0f}, {.fill = ui::Color(0x05080e, 0.85f)});
		render::AddBox(low, {low.x + (PICTURE - 6.0f) * share, low.y + 3.0f}, {.fill = color});
	}
	ui::TextLook name = Small(theme, look.empty ? theme.textDim : theme.text);
	name.size = 12.0f;
	DrawName(ui, look.name, {min.x, to.y + 3.0f}, width, name);
}
} // namespace

std::string TileKey(const Tile& tile)
{
	char key[80];
	std::snprintf(key, sizeof(key), "%d:%s:%d", int(tile.kind), tile.id.c_str(), tile.index);
	return key;
}

std::string ItemName(Resources& resources, const Tile& tile)
{
	StringTable& strings = resources.strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	std::string key;
	switch (tile.kind)
	{
		case TileKind::Resource: key = "resource." + tile.id; break;
		case TileKind::Ammo: key = "ammo." + tile.id; break;
		case TileKind::Module:
		case TileKind::Fitted: key = "module." + tile.id; break;
		case TileKind::EmptySlot:
		{
			const char* kinds[] = {"slot.external", "slot.internal", "slot.rig"};
			return strings.Format("inventory.slot_empty", strings.Get(kinds[u32(tile.slot) % 3]));
		}
		case TileKind::Loot:
			key = catalog.FindModule(tile.id) ? "module." + tile.id
			      : catalog.FindAmmo(tile.id) ? "ammo." + tile.id
			                                  : "resource." + tile.id;
			break;
		case TileKind::Blueprint:
			key = catalog.FindHull(tile.id)     ? "hull." + tile.id
			      : catalog.FindModule(tile.id) ? "module." + tile.id
			                                    : "ammo." + tile.id;
			break;
	}
	return strings.Get(key.c_str());
}

ui::ItemState ItemTile(ui::Context& ui, Resources& resources, const Tile& tile, Place from,
                       bool picked)
{
	StringTable& strings = resources.strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	ui::Layout box;
	box.width = ui::Fixed(TILE_WIDTH);
	box.height = ui::Fixed(TILE_HEIGHT);
	box.padding = ui::All(3.0f);
	ui.BeginItem("tile", box, picked);
	const std::string name = ItemName(resources, tile);
	TileLook look;
	if (tile.kind != TileKind::EmptySlot)
		look.picture = resources.artTextures.Find("icon." + tile.id);
	look.glyph = tile.kind == TileKind::EmptySlot ? "" : icon::Find(tile.id); // lives on
	look.name = ui.CopyArray(name.c_str(), u32(name.size()) + 1);
	const bool counted = tile.kind == TileKind::Resource || tile.kind == TileKind::Ammo ||
	                     tile.kind == TileKind::Loot;
	const std::string count = counted ? Number(tile.count) : std::string();
	look.count = ui.CopyArray(count.c_str(), u32(count.size()) + 1);
	look.condition =
		tile.kind == TileKind::Module || tile.kind == TileKind::Fitted ? tile.condition : 1.0f;
	look.empty = tile.kind == TileKind::EmptySlot;
	ui.Custom("look", ui::Grow(), ui::Grow(), DrawTile, ui.CopyArray(&look, 1));
	const ui::ItemState state = ui.EndItem();
	// What it is, on hover: the volume, the condition.
	if (state.hovered && !ui.Dragging())
	{
		std::string tip = name;
		f32 each = 0.0f;
		f32 mass = 0.0f;
		if (tile.kind != TileKind::EmptySlot && tile.kind != TileKind::Blueprint &&
		    catalog.ItemSize(tile.id, each, mass))
			tip += "\n" + strings.Format("inventory.each", Volume(each).c_str());
		if (tile.kind == TileKind::Module || tile.kind == TileKind::Fitted)
			tip += "\n" + strings.Format("inventory.condition",
			                             Number(f64(tile.condition) * 100.0).c_str());
		ui.Tooltip(tip.c_str());
	}
	Carried carried;
	if (Carry(tile, from, carried))
	{
		ui::DragLook drag;
		drag.text = look.name;
		drag.image = look.picture;
		drag.size = {PICTURE, PICTURE};
		ui.DragSource(DRAG_ITEM, &carried, u32(sizeof(carried)), drag);
	}
	return state;
}

std::string Inventory::PlaceName(Place where) const
{
	StringTable& strings = resources->strings;
	switch (where.kind)
	{
		case PlaceKind::Storage: return strings.Get("inventory.storage");
		case PlaceKind::Blueprints: return strings.Get("hub.blueprints");
		case PlaceKind::Hold:
		case PlaceKind::Fitting: break;
	}
	const sim::ShipRecord* ship = FindShip(*shown, where.ship);
	std::string name = ship ? std::string(strings.Get(("hull." + ship->fit.hull).c_str())) + " #" +
	                              std::to_string(ship->id)
	                        : std::string();
	return name + ": " +
	       strings.Get(where.kind == PlaceKind::Hold ? "inventory.hold" : "inventory.fitting");
}

void Inventory::Drop(const void* payload, Place onto, i32 slot, Asks& asks)
{
	Carried carried;
	std::memcpy(&carried, payload, sizeof(carried));
	sim::Operation operation;
	std::string why;
	if (DropOn(sim::GetCatalog(), *shown, carried, onto, slot, operation, why))
		asks.requests.push_back(operation);
	else if (!why.empty())
		asks.notice = why;
}

void Inventory::PlaceRow(ui::Context& ui, const char* id, const std::string& label,
                         const std::string& note, Place where, bool indented, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	ui::Layout row;
	row.width = ui::Grow();
	row.padding = ui::Sides(indented ? 22.0f : 8.0f, 5.0f);
	ui.BeginItem(id, row, place == where);
	ui::Layout line;
	line.width = ui::Grow();
	line.gap = 6.0f;
	line.align = ui::Align::Center;
	ui.BeginRow(line);
	ui::TextLook look;
	look.size = indented ? theme.smallSize : theme.textSize;
	look.bold = !indented;
	ui.Text(label.c_str(), look);
	ui.Spacer();
	if (!note.empty())
		ui.Text(note.c_str(), Small(theme));
	ui.End();
	const ui::ItemState state = ui.EndItem();
	if (state.clicked && !(place == where))
	{
		place = where;
		picked.clear();
		resources->Play(resources->move);
	}
	if (const void* payload = ui.DropTarget(DRAG_ITEM))
		Drop(payload, where, -1, asks);
}

void Inventory::Build(ui::Context& ui, Resources& from, const sim::Account& account, Asks& asks,
                      bool compact)
{
	resources = &from;
	shown = &account;
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	// A ship that is gone (lost, or never) leaves its place for storage.
	if ((place.kind == PlaceKind::Hold || place.kind == PlaceKind::Fitting) &&
	    !FindShip(account, place.ship))
		place = {};
	tiles = TilesOf(catalog, account, place);
	mobile = compact;
	if (compact)
	{
		BuildCompact(ui, asks);
		return;
	}

	ui::Layout panes;
	panes.width = ui::Grow();
	panes.height = ui::Grow();
	panes.gap = 10.0f;
	panes.align = ui::Align::Stretch;
	ui.BeginRow(panes);

	// The places.
	ui::Layout left;
	left.width = ui::Fixed(PLACES_WIDTH);
	left.height = ui::Grow();
	left.padding = ui::All(8.0f);
	left.gap = 2.0f;
	ui.BeginPanel(left);
	ui::Layout tree;
	tree.width = ui::Grow();
	tree.height = ui::Grow();
	tree.gap = 2.0f;
	ui.BeginScroll("places", tree);
	PlaceRow(ui, "storage",
	         std::string(icon::Find("depot")) + " " + strings.Get("inventory.storage"),
	         Number(f64(TilesOf(catalog, account, {}).size())), {}, false, asks);
	for (const sim::ShipRecord& ship : account.ships)
	{
		ui.PushId(ship.id);
		const sim::HullDesc* hull = catalog.FindHull(ship.fit.hull);
		std::string name = std::string(icon::Find(ship.fit.hull)) + " " +
		                   strings.Get(("hull." + ship.fit.hull).c_str()) + " #" +
		                   std::to_string(ship.id);
		PlaceRow(ui, "ship", name, ship.away ? strings.Get("hub.away") : "",
		         {PlaceKind::Hold, ship.id}, false, asks);
		f32 volume = 0.0f;
		f32 mass = 0.0f;
		sim::HoldSize(catalog, ship.fit.hold, volume, mass);
		PlaceRow(ui, "hold", strings.Get("inventory.hold"),
		         strings.Format("inventory.hold_room", Volume(volume).c_str(),
		                        Volume(hull ? hull->cargo : 0.0f).c_str()),
		         {PlaceKind::Hold, ship.id}, true, asks);
		u32 fitted = 0;
		for (const sim::FittedModule& module : ship.fit.slots)
			fitted += module.id.empty() ? 0 : 1;
		PlaceRow(ui, "fitting", strings.Get("inventory.fitting"),
		         std::to_string(fitted) + " / " + std::to_string(ship.fit.slots.size()),
		         {PlaceKind::Fitting, ship.id}, true, asks);
		ui.PopId();
	}
	PlaceRow(ui, "blueprints", strings.Get("hub.blueprints"),
	         Number(f64(account.blueprints.size())), {PlaceKind::Blueprints, 0}, false, asks);
	ui.EndScroll();
	ui.End();

	BuildItems(ui, asks);
	ui.End();
}

void Inventory::BuildItems(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::Account& account = *shown;
	// The place's items.
	ui::Layout right;
	right.width = ui::Grow();
	right.height = ui::Grow();
	right.padding = ui::All(10.0f);
	right.gap = 8.0f;
	ui.BeginPanel(right);
	ui::Layout head;
	head.width = ui::Grow();
	head.gap = 10.0f;
	head.align = ui::Align::Center;
	ui.BeginRow(head);
	ui::TextLook title;
	title.size = theme.textSize;
	title.bold = true;
	ui.Text(PlaceName(place).c_str(), title);
	ui.Spacer();
	f64 volume = 0.0;
	for (const Tile& tile : tiles)
	{
		f32 each = 0.0f;
		f32 mass = 0.0f;
		if ((tile.kind != TileKind::Blueprint && tile.kind != TileKind::EmptySlot) &&
		    catalog.ItemSize(tile.id, each, mass))
			volume += f64(each) * tile.count;
	}
	ui.Text(strings.Format("inventory.items", std::to_string(tiles.size()).c_str()).c_str(),
	        Small(theme));
	if (place.kind != PlaceKind::Blueprints)
		ui.Text(strings.Format("inventory.volume", Volume(volume).c_str()).c_str(), Small(theme));
	ui.End();
	if (place.kind == PlaceKind::Hold)
	{
		const sim::ShipRecord* ship = FindShip(account, place.ship);
		const sim::HullDesc* hull = ship ? catalog.FindHull(ship->fit.hull) : nullptr;
		if (hull && hull->cargo > 0.0f)
			ui.Progress("room", f32(volume) / hull->cargo, ui::Grow());
	}
	BuildTiles(ui, asks);
	BuildPicked(ui, asks);
	ui.End();
}

void Inventory::BuildCompact(ui::Context& ui, Asks& asks)
{
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::Account& account = *shown;
	ui::Layout column;
	column.width = ui::Grow();
	column.height = ui::Grow();
	column.gap = 6.0f;
	ui.BeginColumn(column);
	// The places as chips: storage, each ship, the blueprints; a ship's
	// chip opens its hold, and a row under it picks the hold or the fitting.
	ui::Layout chips;
	chips.width = ui::Grow();
	chips.gap = 6.0f;
	chips.wrap = true;
	ui.BeginRow(chips);
	PlaceChip(ui, "storage",
	          std::string(icon::Find("depot")) + " " + strings.Get("inventory.storage"), {},
	          place.kind == PlaceKind::Storage, asks);
	for (const sim::ShipRecord& ship : account.ships)
	{
		ui.PushId(ship.id);
		const std::string name = std::string(icon::Find(ship.fit.hull)) + " #" +
		                         std::to_string(ship.id) +
		                         (ship.away ? " " + std::string(strings.Get("hub.away")) : "");
		const bool on = (place.kind == PlaceKind::Hold || place.kind == PlaceKind::Fitting) &&
		                place.ship == ship.id;
		PlaceChip(ui, "ship", name, {PlaceKind::Hold, ship.id}, on, asks);
		ui.PopId();
	}
	PlaceChip(ui, "blueprints", strings.Get("hub.blueprints"), {PlaceKind::Blueprints, 0},
	          place.kind == PlaceKind::Blueprints, asks);
	ui.End();
	if (place.kind == PlaceKind::Hold || place.kind == PlaceKind::Fitting)
	{
		if (const sim::ShipRecord* ship = FindShip(account, place.ship))
		{
			const sim::HullDesc* hull = catalog.FindHull(ship->fit.hull);
			f32 volume = 0.0f;
			f32 mass = 0.0f;
			sim::HoldSize(catalog, ship->fit.hold, volume, mass);
			u32 fitted = 0;
			for (const sim::FittedModule& module : ship->fit.slots)
				fitted += module.id.empty() ? 0 : 1;
			ui::Layout parts;
			parts.width = ui::Grow();
			parts.gap = 6.0f;
			ui.BeginRow(parts);
			PlaceChip(ui, "hold",
			          std::string(strings.Get("inventory.hold")) + " " +
			              strings.Format("inventory.hold_room", Volume(volume).c_str(),
			                             Volume(hull ? hull->cargo : 0.0f).c_str()),
			          {PlaceKind::Hold, ship->id}, place.kind == PlaceKind::Hold, asks, true);
			PlaceChip(ui, "fitting",
			          std::string(strings.Get("inventory.fitting")) + " " + std::to_string(fitted) +
			              "/" + std::to_string(ship->fit.slots.size()),
			          {PlaceKind::Fitting, ship->id}, place.kind == PlaceKind::Fitting, asks, true);
			ui.End();
		}
	}
	BuildItems(ui, asks);
	ui.End();
}

void Inventory::PlaceChip(ui::Context& ui, const char* id, const std::string& label, Place where,
                          bool on, Asks& asks, bool grow)
{
	const ui::Theme& theme = ui.GetTheme();
	ui::Layout chip;
	chip.width = grow ? ui::Grow() : ui::Fit();
	chip.height = ui::Fixed(theme.touchSize > 0.0f ? theme.touchSize - 4.0f : 34.0f);
	chip.padding = ui::Sides(12.0f, 0.0f);
	chip.justify = ui::Justify::Center;
	chip.align = ui::Align::Center;
	ui.BeginItem(id, chip, on);
	ui::TextLook look;
	look.size = theme.smallSize + 1.0f;
	look.bold = on;
	look.color = on ? theme.accent : theme.text;
	ui.Text(label.c_str(), look);
	if (ui.EndItem().clicked && !(place == where))
	{
		place = where;
		picked.clear();
		resources->Play(resources->move);
	}
	if (const void* payload = ui.DropTarget(DRAG_ITEM))
		Drop(payload, where, -1, asks);
}

void Inventory::BuildTiles(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	ui::Layout area;
	area.width = ui::Grow();
	area.height = ui::Grow();
	area.padding = ui::All(4.0f);
	ui.BeginScroll("tiles", area);
	ui::Layout grid;
	grid.width = ui::Grow();
	grid.gap = 6.0f;
	grid.wrap = true;
	ui.BeginRow(grid);
	if (tiles.empty())
		ui.Text(strings.Get("inventory.empty"), Small(theme));
	for (u32 i = 0; i < tiles.size(); ++i)
	{
		const Tile& tile = tiles[i];
		const std::string key = TileKey(tile);
		ui.PushId(i);
		const ui::ItemState state = ItemTile(ui, *resources, tile, place, picked == key);
		if (state.clicked)
		{
			picked = key;
			resources->Play(resources->move);
		}
		// A fitting's slots take a module each.
		if (place.kind == PlaceKind::Fitting)
		{
			if (const void* payload = ui.DropTarget(DRAG_ITEM))
				Drop(payload, place, tile.index, asks);
		}
		ui.PopId();
	}
	ui.End();
	ui.EndScroll();
	// Dropped anywhere on the grid: into this place.
	if (const void* payload = ui.DropTarget(DRAG_ITEM))
		Drop(payload, place, -1, asks);
}

void Inventory::BuildPicked(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const Tile* tile = nullptr;
	for (const Tile& each : tiles)
	{
		if (TileKey(each) == picked)
			tile = &each;
	}
	ui::Layout strip;
	strip.width = ui::Grow();
	strip.gap = 10.0f;
	strip.align = ui::Align::Center;
	ui.BeginRow(strip);
	if (!tile)
	{
		if (!mobile) // phones: fewer words
		{
			ui::TextLook hint = Small(theme);
			hint.wrap = true;
			ui.Text(strings.Get("inventory.hint"), hint);
		}
		ui.End();
		return;
	}
	ui::TextLook bold;
	bold.size = theme.textSize;
	bold.bold = true;
	ui.Text(ItemName(*resources, *tile).c_str(), bold);
	if (tile->kind == TileKind::Blueprint)
	{
		// What it costs, and the button to build it.
		sim::Resources cost;
		if (const sim::HullDesc* hull = catalog.FindHull(tile->id))
			cost = hull->cost;
		else if (const sim::ModuleDesc* module = catalog.FindModule(tile->id))
			cost = module->cost;
		else if (const sim::AmmoDesc* ammo = catalog.FindAmmo(tile->id))
			cost = ammo->cost;
		bool affordable = true;
		for (u32 r = 0; r < sim::RESOURCES; ++r)
		{
			if (cost.amount[r] <= 0.0)
				continue;
			const bool lacking = shown->resources.amount[r] + 1e-6 < cost.amount[r];
			affordable = affordable && !lacking;
			const std::string text = std::string(icon::Find(sim::ResourceId(sim::Resource(r)))) +
			                         " " + Number(cost.amount[r]);
			ui.Text(text.c_str(), Small(theme, lacking ? WARN : theme.text));
		}
		ui.Spacer();
		if (ui.Button(strings.Get("hub.build"),
		              affordable ? ui::ButtonKind::Primary : ui::ButtonKind::Normal))
		{
			sim::Operation operation;
			operation.kind = sim::Operation::Kind::Build;
			operation.id = tile->id;
			asks.requests.push_back(operation);
		}
	}
	else if (tile->kind == TileKind::Fitted)
	{
		ui.Spacer();
		if (ui.Button(strings.Get("hub.remove")))
		{
			sim::Operation operation;
			operation.kind = sim::Operation::Kind::Fit;
			operation.ship = place.ship;
			operation.slot = u32(tile->index);
			operation.module = -1;
			asks.requests.push_back(operation);
		}
	}
	ui.End();
}
} // namespace sn
