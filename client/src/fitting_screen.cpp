#include "fitting_screen.h"

#include "icon_codes.h"
#include "inventory.h"
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

constexpr f32 SIDE_WIDTH = 300.0f;   // the ships and modules
constexpr f32 STATS_WIDTH = 340.0f;  // the numbers
constexpr f32 SOCKET = 58.0f;        // a slot's socket, across
constexpr f32 RING = 0.40f;          // the ring's radius, of the region's shorter side
constexpr f32 RING_COMPACT = 270.0f; // the ring's height on a phone
constexpr f32 STEP = 0.36f;          // radians between sockets
constexpr f32 PI = 3.14159265f;

const u32 WARN = ui::Color(0xff6b5b);
const u32 GOOD = ui::Color(0x5ee38a);
const u32 GOLD = ui::Color(0xffc857);
const u32 ACCENT = ui::Color(0x71b2d8);

ui::TextLook Small(const ui::Theme& theme, u32 color = 0)
{
	ui::TextLook look;
	look.size = theme.smallSize;
	look.color = color ? color : theme.textDim;
	return look;
}

ui::TextLook Heading(const ui::Theme& theme)
{
	ui::TextLook look;
	look.size = theme.smallSize;
	look.bold = true;
	look.color = ACCENT;
	return look;
}

std::string Number(f64 value, const char* format = "%.0f")
{
	char text[32];
	std::snprintf(text, sizeof(text), format, value);
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

// The ring (RING of its region's shorter side) and its slot kinds' arcs,
// under the sockets.
struct RingLook
{
	f32 arcs[3][2] = {}; // each kind's from-to angles; equal: none
};

void DrawRing(ui::Context& ui, const void* data, Vec2 min, Vec2 max)
{
	const RingLook& look = *static_cast<const RingLook*>(data);
	const Vec2 middle = (min + max) * 0.5f;
	const f32 radius = RING * std::min(max.x - min.x, max.y - min.y);
	const f32 border = std::max(ui.GetTheme().border, 0.5f);
	render::AddArc(middle, radius + 20.0f, border, 0.0f, 2.0f * PI, ui::Color(0x3e4d6b, 0.7f));
	render::AddArc(middle, radius - 20.0f, border, 0.0f, 2.0f * PI, ui::Color(0x3e4d6b, 0.45f));
	// Each kind's arc, brighter, behind its sockets.
	const u32 colors[3] = {ui::Color(0x4e87bb, 0.55f), ui::Color(0x5ee38a, 0.4f),
	                       ui::Color(0xffc857, 0.4f)};
	for (u32 k = 0; k < 3; ++k)
	{
		if (look.arcs[k][0] != look.arcs[k][1])
			render::AddArc(middle, radius + 3.0f, 6.0f, look.arcs[k][0], look.arcs[k][1],
			               colors[k]);
	}
}

// A socket: its module's picture in a round frame, or the empty slot.
struct SocketLook
{
	rhi::Texture picture;
	const char* glyph = "";
	f32 condition = 1.0f;
	u32 kind = 0;
	bool empty = true;
};

void DrawSocket(ui::Context& ui, const void* data, Vec2 min, Vec2 max)
{
	const SocketLook& look = *static_cast<const SocketLook*>(data);
	const Vec2 middle = (min + max) * 0.5f;
	const f32 radius = 0.5f * std::min(max.x - min.x, max.y - min.y);
	render::AddCircle(middle, radius, ui::Color(0x0a0f16, 0.9f));
	if (!look.empty && look.picture)
		render::AddImage({middle.x - radius + 2.0f, middle.y - radius + 2.0f},
		                 {middle.x + radius - 2.0f, middle.y + radius - 2.0f}, look.picture,
		                 0xffffffff, {0.0f, 0.0f, 1.0f, 1.0f}, radius - 2.0f);
	else if (look.glyph[0])
	{
		ui::TextLook glyph;
		glyph.size = 24.0f;
		glyph.color = look.empty ? ui::Color(0x3e4d6b) : ui::Color(0x97999f);
		const Vec2 size = ui.MeasureText(look.glyph, glyph);
		ui.DrawText(look.glyph, {middle.x - 0.5f * size.x, middle.y - 0.5f * size.y}, glyph);
	}
	const u32 kinds[3] = {ui::Color(0x4e87bb), ui::Color(0x5ee38a), ui::Color(0xffc857)};
	render::AddArc(middle, radius, 1.5f, 0.0f, 2.0f * PI,
	               look.empty ? ui::Color(0x3e4d6b) : kinds[look.kind % 3]);
	// A worn module's condition around its frame.
	if (!look.empty && look.condition < 0.999f)
	{
		const f32 share = std::clamp(look.condition, 0.0f, 1.0f);
		const u32 color = share > 0.6f ? GOOD : share > 0.3f ? GOLD : WARN;
		render::AddArc(middle, radius + 4.0f, 2.5f, -0.5f * PI, -0.5f * PI + 2.0f * PI * share,
		               color);
	}
}

// Where each slot's socket goes, clockwise on screen from +x: the externals
// across the top, the internals down the right, the rigs along the bottom.
f32 SocketAngle(sim::SlotKind kind, u32 index, u32 count)
{
	const f32 centers[3] = {-0.5f * PI, 0.12f * PI, 0.62f * PI};
	return centers[u32(kind) % 3] + (f32(index) - 0.5f * f32(count - 1)) * STEP;
}
} // namespace

bool FittingScreen::ShipRegion(Vec2& min, Vec2& max) const
{
	if (!ringShown)
		return false;
	min = ringMin;
	max = ringMax;
	return true;
}

void FittingScreen::Drop(const void* payload, Place onto, i32 slot, Asks& asks)
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

void FittingScreen::Build(ui::Context& ui, Resources& from, const sim::Account& account, Asks& asks,
                          bool compactLayout)
{
	resources = &from;
	shown = &account;
	compact = compactLayout;
	ringShown = false;
	if (!FindShip(account, ship))
		ship = account.ships.empty() ? 0 : account.ships.front().id;
	if (compact)
	{
		BuildCompact(ui, asks);
		return;
	}
	ui::Layout body;
	body.width = ui::Grow();
	body.height = ui::Grow();
	body.gap = 10.0f;
	body.align = ui::Align::Stretch;
	ui.BeginRow(body);
	BuildShips(ui, asks);
	if (const sim::ShipRecord* record = FindShip(account, ship))
	{
		BuildRing(ui, *record, asks);
		BuildStats(ui, *record);
	}
	else
		ui.Spacer();
	ui.End();
}

void FittingScreen::BuildShips(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	ui::Layout side;
	side.width = ui::Fixed(SIDE_WIDTH);
	side.height = ui::Grow();
	side.padding = ui::All(8.0f);
	side.gap = 4.0f;
	ui.BeginPanel(side);
	ui.Text(strings.Get("hub.ships"), Heading(theme));
	for (const sim::ShipRecord& record : shown->ships)
	{
		ui.PushId(record.id);
		ui::Layout row;
		row.width = ui::Grow();
		row.padding = ui::Sides(8.0f, 5.0f);
		row.gap = 3.0f;
		ui.BeginItem("ship", row, record.id == ship);
		ui::Layout line;
		line.width = ui::Grow();
		line.align = ui::Align::Center;
		line.gap = 6.0f;
		ui.BeginRow(line);
		const std::string name = std::string(icon::Find(record.fit.hull)) + " " +
		                         strings.Get(("hull." + record.fit.hull).c_str()) + " #" +
		                         std::to_string(record.id);
		ui.Text(name.c_str());
		ui.Spacer();
		if (record.away)
			ui.Text(strings.Get("hub.away"), Small(theme, GOLD));
		ui.End();
		ui.Progress("condition", record.fit.condition, ui::Grow(),
		            record.fit.condition < 0.5f ? WARN : GOOD);
		const ui::ItemState state = ui.EndItem();
		if (state.clicked && record.id != ship)
		{
			ship = record.id;
			picked.clear();
			resources->Play(resources->move);
		}
		ui.PopId();
	}
	if (shown->ships.empty())
		ui.Text(strings.Get("hub.no_ships"), Small(theme));

	ui.Separator();
	ui.Text(strings.Get("fitting.fits"), Heading(theme));
	BuildModules(ui, asks);
	ui.End();
}

void FittingScreen::BuildModules(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	// Storage's modules that fit this hull: dragged onto a slot to fit; a
	// fitted module dragged here goes back to storage.
	const sim::ShipRecord* record = FindShip(*shown, ship);
	const sim::HullDesc* hull = record ? catalog.FindHull(record->fit.hull) : nullptr;
	const std::vector<sim::SlotKind> kinds =
		hull ? sim::SlotsOf(*hull) : std::vector<sim::SlotKind>();
	ui::Layout area;
	area.width = ui::Grow();
	area.height = ui::Grow();
	ui.BeginScroll("modules", area);
	ui::Layout grid;
	grid.width = ui::Grow();
	grid.gap = 6.0f;
	grid.wrap = true;
	ui.BeginRow(grid);
	const Place storage = {};
	u32 shownCount = 0;
	for (const Tile& tile : TilesOf(catalog, *shown, storage))
	{
		const sim::ModuleDesc* module =
			tile.kind == TileKind::Module ? catalog.FindModule(tile.id) : nullptr;
		if (!module || std::find(kinds.begin(), kinds.end(), module->slot) == kinds.end())
			continue;
		++shownCount;
		ui.PushId(u32(tile.index));
		const std::string key = TileKey(tile);
		const ui::ItemState state = ItemTile(ui, *resources, tile, storage, picked == key);
		if (state.clicked)
			picked = key;
		// A double click fits it where it goes.
		if (state.doubleClicked && record)
		{
			Carried carried;
			sim::Operation operation;
			std::string why;
			if (Carry(tile, storage, carried) &&
			    DropOn(catalog, *shown, carried, {PlaceKind::Fitting, ship}, -1, operation, why))
				asks.requests.push_back(operation);
			else if (!why.empty())
				asks.notice = why;
		}
		ui.PopId();
	}
	if (shownCount == 0)
		ui.Text(strings.Get("hub.none_fits"), Small(theme));
	ui.End();
	ui.EndScroll();
	if (const void* payload = ui.DropTarget(DRAG_ITEM))
		Drop(payload, storage, -1, asks);
}

void FittingScreen::BuildCompact(ui::Context& ui, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	ui::Layout column;
	column.width = ui::Grow();
	column.height = ui::Grow();
	column.gap = 6.0f;
	ui.BeginColumn(column);
	// The ships as chips.
	ui::Layout chips;
	chips.width = ui::Grow();
	chips.gap = 6.0f;
	chips.wrap = true;
	ui.BeginRow(chips);
	for (const sim::ShipRecord& record : shown->ships)
	{
		ui.PushId(record.id);
		ui::Layout chip;
		chip.height = ui::Fixed(theme.touchSize - 4.0f);
		chip.padding = ui::Sides(12.0f, 0.0f);
		chip.justify = ui::Justify::Center;
		chip.align = ui::Align::Center;
		const bool on = record.id == ship;
		ui.BeginItem("ship", chip, on);
		ui::TextLook look;
		look.size = theme.smallSize + 1.0f;
		look.bold = on;
		look.color = on ? theme.accent : record.fit.condition < 0.5f ? WARN : theme.text;
		const std::string name = std::string(icon::Find(record.fit.hull)) + " #" +
		                         std::to_string(record.id) + " " +
		                         Number(f64(record.fit.condition) * 100.0) + "%";
		ui.Text(name.c_str(), look);
		if (ui.EndItem().clicked && !on)
		{
			ship = record.id;
			picked.clear();
			resources->Play(resources->move);
		}
		ui.PopId();
	}
	ui.End();
	const sim::ShipRecord* record = FindShip(*shown, ship);
	if (!record)
	{
		ui.Text(strings.Get("hub.no_ships"), Small(theme));
		ui.End();
		return;
	}
	BuildRing(ui, *record, asks);
	// Under the ring, the modules or the numbers.
	ui::Layout tabs;
	tabs.width = ui::Grow();
	tabs.gap = 6.0f;
	ui.BeginRow(tabs);
	const char* panes[2] = {"fitting.fits", "hub.stats"};
	for (u32 i = 0; i < 2; ++i)
	{
		ui::Layout cell;
		cell.width = ui::Grow();
		cell.height = ui::Fixed(theme.touchSize - 4.0f);
		cell.justify = ui::Justify::Center;
		cell.align = ui::Align::Center;
		const bool on = (i == 1) == numbers;
		ui.BeginItem(panes[i], cell, on);
		ui::TextLook look;
		look.size = theme.smallSize + 1.0f;
		look.bold = on;
		look.color = on ? theme.accent : theme.text;
		ui.Text(strings.Get(panes[i]), look);
		if (ui.EndItem().clicked)
			numbers = i == 1;
	}
	ui.End();
	if (numbers)
	{
		ui::Layout area;
		area.width = ui::Grow();
		area.height = ui::Grow();
		area.gap = 4.0f;
		ui.BeginScroll("numbers", area);
		BuildNumbers(ui, *record);
		ui.EndScroll();
	}
	else
		BuildModules(ui, asks);
	ui.End();
}

void FittingScreen::BuildRing(ui::Context& ui, const sim::ShipRecord& record, Asks& asks)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	const sim::Catalog& catalog = sim::GetCatalog();
	const sim::HullDesc* hull = catalog.FindHull(record.fit.hull);
	if (!hull)
		return;
	ui::Layout middle;
	middle.width = ui::Grow();
	middle.height = compact ? ui::Fit() : ui::Grow();
	middle.gap = 6.0f;
	ui.BeginColumn(middle);

	// The ship's name and class, and its repair.
	ui::Layout head;
	head.width = ui::Grow();
	head.gap = 10.0f;
	head.align = ui::Align::Center;
	ui.BeginRow(head);
	ui::TextLook title;
	title.size = theme.textSize + 2.0f;
	title.bold = true;
	const std::string name =
		std::string(strings.Get(("hull." + hull->id).c_str())) + " #" + std::to_string(record.id);
	ui.Text(name.c_str(), title);
	ui.Text(strings.Get(("class." + hull->shipClass).c_str()), Small(theme));
	ui.Spacer();
	const f64 repair = sim::RepairCost(catalog, record);
	if (repair > 0.0 && !record.away)
	{
		const std::string label = strings.Format("hub.repair", Number(repair).c_str());
		if (shown->resources[sim::Resource::Metal] + 1e-6 >= repair)
		{
			if (ui.Button(label.c_str()))
			{
				sim::Operation operation;
				operation.kind = sim::Operation::Kind::Repair;
				operation.ship = record.id;
				asks.requests.push_back(operation);
			}
		}
		else
			ui.Text(label.c_str(), Small(theme, WARN));
	}
	ui.End();
	if (record.away)
		ui.Text(strings.Get("hub.away_note"), Small(theme, GOLD));

	// The ring: the hub draws the ship inside it; the slots sit on it.
	ui::Layout box;
	box.width = ui::Grow();
	box.height = compact ? ui::Fixed(RING_COMPACT) : ui::Grow();
	ui.BeginBox(false, box, {});
	const std::vector<sim::SlotKind> kinds = sim::SlotsOf(*hull);
	u32 counts[3] = {};
	for (const sim::SlotKind kind : kinds)
		++counts[u32(kind) % 3];
	RingLook ring;
	for (u32 k = 0; k < 3; ++k)
	{
		if (counts[k] == 0)
			continue;
		ring.arcs[k][0] = SocketAngle(sim::SlotKind(k), 0, counts[k]) - 0.5f * STEP;
		ring.arcs[k][1] = SocketAngle(sim::SlotKind(k), counts[k] - 1, counts[k]) + 0.5f * STEP;
	}
	ui.Custom("ring", ui::Grow(), ui::Grow(), DrawRing, ui.CopyArray(&ring, 1));
	// Where it was laid out last frame: the sockets go round it.
	Vec2 low;
	Vec2 high;
	const bool laidOut = ui.LastRect(ui.LastId(), low, high);
	const f32 radius = laidOut ? RING * std::min(high.x - low.x, high.y - low.y) : 0.0f;
	if (radius > SOCKET)
	{
		ringShown = true;
		const Vec2 center = (low + high) * 0.5f;
		const f32 inner = radius - 0.5f * SOCKET - 8.0f;
		ringMin = {center.x - inner, center.y - inner};
		ringMax = {center.x + inner, center.y + inner};
		u32 seen[3] = {};
		for (u32 slot = 0; slot < kinds.size() && slot < record.fit.slots.size(); ++slot)
		{
			const u32 kind = u32(kinds[slot]) % 3;
			const f32 angle = SocketAngle(kinds[slot], seen[kind]++, counts[kind]);
			const Vec2 at = {center.x + radius * std::cos(angle) - low.x,
			                 center.y + radius * std::sin(angle) - low.y};
			const sim::FittedModule& fitted = record.fit.slots[slot];
			Tile tile;
			tile.kind = fitted.id.empty() ? TileKind::EmptySlot : TileKind::Fitted;
			tile.id = fitted.id;
			tile.condition = fitted.condition;
			tile.index = i32(slot);
			tile.slot = kinds[slot];
			ui.PushId(slot);
			ui::Layout socket;
			socket.width = ui::Fixed(SOCKET);
			socket.height = ui::Fixed(SOCKET);
			ui.BeginFloat("socket", at, socket, {0.5f, 0.5f});
			const std::string key = TileKey(tile);
			ui::Layout inside;
			inside.width = ui::Grow();
			inside.height = ui::Grow();
			ui.BeginItem("item", inside, picked == key);
			SocketLook look;
			look.empty = fitted.id.empty();
			look.kind = kind;
			look.condition = fitted.condition;
			if (!look.empty)
			{
				look.picture = resources->artTextures.Find("icon." + fitted.id);
				look.glyph = icon::Find(fitted.id);
			}
			ui.Custom("look", ui::Grow(), ui::Grow(), DrawSocket, ui.CopyArray(&look, 1));
			const ui::ItemState state = ui.EndItem();
			const std::string tip = ItemName(*resources, tile);
			if (state.hovered && !ui.Dragging())
				ui.Tooltip(tip.c_str());
			if (state.clicked)
				picked = key;
			// Out of its slot: dragged back to the modules, or a double click.
			Carried carried;
			if (!record.away && Carry(tile, {PlaceKind::Fitting, record.id}, carried))
			{
				ui::DragLook drag;
				drag.text = ui.CopyArray(tip.c_str(), u32(tip.size()) + 1);
				drag.image = look.picture;
				drag.size = {48.0f, 48.0f};
				ui.DragSource(DRAG_ITEM, &carried, u32(sizeof(carried)), drag);
				if (state.doubleClicked)
				{
					sim::Operation operation;
					operation.kind = sim::Operation::Kind::Fit;
					operation.ship = record.id;
					operation.slot = slot;
					operation.module = -1;
					asks.requests.push_back(operation);
				}
			}
			if (!record.away)
			{
				if (const void* payload = ui.DropTarget(DRAG_ITEM))
					Drop(payload, {PlaceKind::Fitting, record.id}, i32(slot), asks);
			}
			ui.EndFloat();
			ui.PopId();
		}
	}
	ui.End();
	// Dropped on the ring but off the sockets: the first slot that fits.
	if (!record.away)
	{
		if (const void* payload = ui.DropTarget(DRAG_ITEM))
			Drop(payload, {PlaceKind::Fitting, record.id}, -1, asks);
	}
	if (!compact)
		ui.Text(strings.Get("fitting.hint"), Small(theme));
	ui.End();
}

void FittingScreen::BuildStats(ui::Context& ui, const sim::ShipRecord& record)
{
	ui::Layout side;
	side.width = ui::Fixed(STATS_WIDTH);
	side.height = ui::Grow();
	side.padding = ui::All(10.0f);
	side.gap = 4.0f;
	ui.BeginPanel(side);
	BuildNumbers(ui, record);
	ui.End();
}

void FittingScreen::BuildNumbers(ui::Context& ui, const sim::ShipRecord& record)
{
	const ui::Theme& theme = ui.GetTheme();
	StringTable& strings = resources->strings;
	sim::Ship made;
	if (!sim::MakeShip(sim::GetCatalog(), record.fit, made))
		return;
	const sim::FitStats stats = sim::StatsOf(made);
	const auto section = [&](const char* key, const char* glyph)
	{
		const std::string text = std::string(icon::Find(glyph)) + " " + strings.Get(key);
		ui.Text(text.c_str(), Heading(theme));
	};
	const auto line = [&](const char* key, const std::string& value, u32 color = 0)
	{
		ui::Layout row;
		row.width = ui::Grow();
		row.gap = 8.0f;
		ui.BeginRow(row);
		ui.Text(strings.Get(key), Small(theme));
		ui.Spacer();
		ui.Text(value.c_str(), Small(theme, color ? color : theme.text));
		ui.End();
	};
	// The reactor and the capacitor.
	section("fitting.power", "power");
	line("stat.power", Number(stats.load) + " / " + Number(stats.reactor) + " MW",
	     stats.Overloaded() ? WARN : 0);
	ui.Progress("power", stats.reactor > 0.0f ? stats.load / stats.reactor : 0.0f, ui::Grow(),
	            stats.Overloaded() ? WARN : ACCENT);
	line("stat.capacitor", Number(stats.capacitor, "%.2f") + " GJ");
	line("fitting.recharge", Number(stats.recharge, "%+.3f") + " GJ/s",
	     stats.recharge < 0.0f ? WARN : 0);
	line("stat.sustain",
	     stats.sustain > 0.0f ? Number(stats.sustain) + " s" : strings.Get("stat.forever"),
	     stats.sustain > 0.0f && stats.sustain < 30.0f ? WARN : 0);
	ui.Separator();
	// Defense: the hull, the shield, the resistances.
	section("fitting.defense", "shield");
	line("stat.hull", Number(f64(made.health)) + " / " + Number(stats.hull));
	line("stat.shield",
	     Number(stats.shield) + " +" + Number(stats.shieldRegen + stats.boost) + "/s");
	line("fitting.threshold", Number(stats.shieldThreshold));
	static constexpr const char* DAMAGE_KEYS[sim::DAMAGE_TYPES] = {
		"damage.em", "damage.explosive", "damage.kinetic", "damage.thermal"};
	static constexpr const char* DAMAGE_ICONS[sim::DAMAGE_TYPES] = {"em", "explosive", "kinetic",
	                                                                "thermal"};
	static const u32 DAMAGE_COLORS[sim::DAMAGE_TYPES] = {ui::Color(0x71b2d8), ui::Color(0xffa24c),
	                                                     ui::Color(0xb8c4d6), ui::Color(0xff6b5b)};
	for (u32 t = 0; t < sim::DAMAGE_TYPES; ++t)
	{
		ui.PushId(t);
		ui::Layout row;
		row.width = ui::Grow();
		row.gap = 8.0f;
		row.align = ui::Align::Center;
		ui.BeginRow(row);
		ui::Layout label;
		label.width = ui::Fixed(110.0f);
		ui.BeginColumn(label);
		const std::string text =
			std::string(icon::Find(DAMAGE_ICONS[t])) + " " + strings.Get(DAMAGE_KEYS[t]);
		ui.Text(text.c_str(), Small(theme));
		ui.End();
		ui.Progress("resist", stats.resist[t], ui::Grow(), DAMAGE_COLORS[t]);
		ui.Text(Number(stats.resist[t] * 100.0, "%.0f%%").c_str(), Small(theme, theme.text));
		ui.End();
		ui.PopId();
	}
	ui.Separator();
	// Weapons.
	section("fitting.weapons", "damage");
	line("stat.dps", Number(stats.dps.Total(), "%.1f"), GOLD);
	line("fitting.range", Number(stats.range) + " m");
	line("fitting.ammo", Number(stats.ammo));
	ui.Separator();
	// Navigation and the hold.
	section("fitting.navigation", "speed");
	line("stat.speed", Number(stats.maxSpeed) + " m/s");
	line("fitting.acceleration", Number(stats.acceleration, "%.1f") + " m/s2");
	line("stat.turn", Number(stats.turnRate * 57.2958f) + " deg/s");
	line("stat.mass", Number(stats.mass, "%.1f") + " t");
	line("stat.cargo", Number(stats.cargoUsed, "%.1f") + " / " + Number(stats.cargo) + " m3");
	if (stats.Overloaded())
	{
		ui::TextLook wrap = Small(theme, WARN);
		wrap.wrap = true;
		ui.Text(strings.Get("hub.overloaded"), wrap);
	}
}
} // namespace sn
