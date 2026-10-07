# 0017. The inventory: every owned thing as an item, moved by dragging

- Status: Proposed
- Date: 2026-10-05

## Context

- The developer (2026-10-05): the game should look grown up and be
  convenient, with smaller interface elements and thinner lines, starting
  with an inventory like EVE Online's cargo window, then a fitting screen.
  "I want to see my 10 turrets each as a separate item in the inventory.
  Metal, He-3 and chips too." A way to try dragging alone:
  `--inventory_test` or another app.
- The account (ADR 0014, `sim/account.h`) already holds everything:
  - resources (metal, He-3, chips) as amounts;
  - ammo in storage as stacks (`items`);
  - modules in storage one by one, each with its condition (`modules`, at
    most 60);
  - ships (at most 12), each with its slots and its hold (stacks).
- The server already has the moves the window needs (`sim::Operation`):
  `fit` (a stored module into a slot, a fitted one back with module -1) and
  `load` (ammo between storage and a hold, either way). Resources and
  modules do not go into holds by request; battles fill holds with loot,
  which comes home into storage.
- pith's UI had no dragging, scrolling, pictures or wrapping rows: they are
  added to ph::ui (pith ADR 0037's addendum of 2026-10-05).

## Decision

- **The Storage tab becomes the inventory**, in two panes as EVE's:
  - on the left, the places: the colony's storage, each ship (its hold and
    its fitting under it) and the blueprints;
  - on the right, the place's items as tiles in a wrapping, scrolling grid:
    a picture, the count on it (stacks and resources), the name under it,
    a condition bar on a damaged module. Each stored module is its own
    tile, so ten turrets are ten tiles. Resources are tiles too.
  - A tile's tooltip tells its volume, condition and slot; the strip under
    the grid sums the place (items, volume, the hold's room).
  - Blueprints show as tiles with their costs; one picked shows its Build
    button.
- **Dragging is the move**, the server deciding as for buttons:
  - ammo from storage onto a ship (its node, its hold node or its hold's
    grid): `load`, as many as fit; from a hold onto the storage: `load`
    back, all of it;
  - a stored module onto a ship or its fitting: `fit` into the first empty
    slot of its kind (else the first of its kind, the old module going to
    storage); a fitted module onto the storage: `fit` with -1;
  - anything else (resources, a ship that is away) is refused with a
    notice. No new operations, no protocol change.
- **Pictures:** generated item pictures (the `icon` kind in
  `content/gen.json`) in the art pack as `icon.<id>` (256 pixels with mip
  levels); an item without one shows its glyph from the icons font on a
  dark tile.
- **Denser look:** the hub's theme gets smaller text, padding and corners,
  and hairline borders (pith's `Theme::border`).
- **`--inventory-test`** (the game's option style; the developer wrote
  `--inventory_test`): the local hub on an account kept in memory only,
  given many things to drag (`server::HubDesc::seed`): ten of each turret,
  boosters and batteries, some damaged, thousands of charges, three
  Lancers. It opens on the inventory; nothing is saved.

## Consequences

- No server change beyond the seed: the window is a new face on the moves
  the sim has.
- The fitting screen (next) reuses the tiles and the dragging.
- Splitting a stack (dragging part of it), moving resources and modules
  into holds, and selling come later, with operations of their own.

## Built on 2026-10-05

- `client/src/inventory_items.h`: the places, their tiles and the drops as
  the server's operations (tested in `sn_tests`, the sim agreeing with
  every operation a drop makes); `inventory.h`: the Inventory tab;
  `fitting_screen.h` and `ship_view.h`: the Fitting tab (it was the
  Hangar), the ship drawn between the background and the interface.
- The seed (`server::HubDesc::seed`, tested): ten turrets of each kind,
  four boosters and four batteries, every third one worn, 3,000 charges,
  three Lancers.
- Checked in the game with the real mouse (SendInput): charges onto a
  ship (2,000, its hold full), a laser onto a ship's fitting, a battery
  onto an internal slot, a laser out of its slot to the modules; the
  numbers follow. The web build shows both screens.
- The hub lays out on 1600x900 units (was 1280x720), with 0.6-unit
  borders. On phones the text gets small: a touch device may want the
  old size back.

## Addendum (2026-10-05): phones, and the house style

- **Phones** (the developer: text too small, buttons too high and too
  small): the hub's units are never smaller than the screen's dp
  (`os::GetWindowScale`, Hub::SetDensity), and a screen narrower than 1000
  units takes the compact layout:
  - the tabs in a bar at the bottom (an icon over each name, 58 units
    high), Leave and the name at the top, the resources on one line;
  - the colony's scene on top and its cards in a list under it, scrolled;
  - the inventory's places as chips over the grid, a ship's chip opening
    a row for its hold and its fitting;
  - the fitting's ships as chips, the ring at a fixed height, and under it
    the modules or the numbers (a switch);
  - the map over the node's panel, Launch at full width;
  - buttons and toggles at least 48 units high (pith's `Theme::touchSize`).
  The colony's scene fits its region of the screen (ColonyView: the lens
  shifted to the region's middle, zoomed so the cluster fits), on phones
  and on desktops alike. The battle's HUD keeps 620 units across (Ui::Begin's
  minWidth), and the menu and the battle say "tap" on touch screens.
  pith's UI scrolls by swiping and drags after a hold on touch (its ADR
  0037 addendum).
- **The house style** (the developer's reference: EVE Online's
  corporation dashboard concept, kept in `refs/ui/`): near-black glass
  panels with one-pixel edges and small corners, small light type, cyan as
  the only accent, primary buttons tinted rather than filled, thin bars.
- **Clear of a phone's bars** (the AGM Glory G1S showed the hub under its
  status and navigation bars): the hub pads its layout by pith's
  `os::GetSafeInsets`, the battle's HUD anchors to them (Ui::Top and
  Ui::Bottom), and on narrow screens the HUD's bars are shorter, so it
  keeps 440 units across and its text grows.
- **On the AGM** (Adreno 619, its 2021 driver): the colony's screen lost
  its 3D scene, and some frames flickered with copies of one screen tile
  (text among them). The colony now draws its sky first, which avoids both
  (pith's `docs/targets/android.md`, 2026-10-05); every screen draws right
  there. With the scene drawn, the colony's frames take 17.7 ms on average
  there (41 ms at worst), the eight buildings about 140,000 triangles each.
