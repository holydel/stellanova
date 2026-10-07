# The solo loop: the first playable game (MVP)

The developer's design of 2026-10-04, with the answers of the same day.
Everything here is open to change. We expect many experiments before the
loop feels right. The numbers live in `content/catalog.json`. This page
says what they mean and gives the starting values. Decisions:
ADR 0013 (ships, modules, damage, orders), ADR 0014 (accounts and the
loop on our server), ADR 0015 (the website).

## The loop

1. The player has a **colony** that produces metal, helium-3 (He-3) and chips over
   time, even while the game is closed, and refills **command points** (the
   loop's stamina).
2. Around the colony lies an **endless star map** of hex rings. Ring 1 is
   next to home, and each ring outward is harder. Fog hides everything
   beyond the next two rings of the explored area.
3. The player picks a node next to explored space, picks **fitted ships**
   with full tanks and loaded cargo, and launches. That costs a command
   point and He-3 fuel.
4. The **battle** is played on our server, with orders and automatic
   turrets. Destroyed enemies drop loot crates; ships pick them up by
   flying over them while their cargo has room.
5. The surviving ships **return to the colony** with what their holds
   carry. Destroyed ships are lost, with their modules and cargo.
6. Loot pays for **colony upgrades** (more production, storage, command
   points), new ships and modules built from **blueprints**, and repairs.
7. **Leaderboard:** the deepest ring cleared, by players signed in with
   Steam. A player starts at once as a guest: the whole loop, but nothing
   is saved and the leaderboard does not list it; signing in saves the
   colony from then on (ADR 0016).

This is the skirmish mode of `docs/vision.md` ("after a win, choose the
next, harder tier") in its solo form: the map's nodes are the tiers.

Solo battles run on our server even when the player is alone, because only
our servers grant account progress (ADR 0002). Offline play stays the
tutorial and practice.

## Sizes

| Size | Classes |
| --- | --- |
| S | frigates, destroyers |
| M | cruisers, battlecruisers |
| L | battleships, siege battleships |
| XL | carriers, dreadnoughts, starbases |

A module has a size too. Slots take modules of their ship's size; the
mixing rules come later. The MVP has one size S frigate.

## Damage

Four types. Every hit carries some of each (most weapons only one or two):

| Type | Against shields |
| --- | --- |
| EM | the shield takes it all while it lasts |
| Explosive | the shield takes it all while it lasts |
| Kinetic | the shield takes up to its **threshold** of each hit; the rest goes to the hull |
| Thermal | as kinetic (one threshold for a hit's kinetic and thermal together) |

- **Resistances** (per type, 0 to 0.9) cut damage before it reaches the
  shield or the hull. One set per ship, for both.
- **The threshold rule** (decided 2026-10-04): with threshold T, a kinetic
  hit of 30 takes 10 from the shield and 20 from the hull; a hit of 6
  takes 6 from the shield. Small, fast hits are stopped; big ones punch
  through.
- When the shield runs out in the middle of a hit, the hull takes the rest.
- A channeled beam (the laser) "hits" once a tick.
- **Modules take damage.** Each hit on the hull also damages one external
  module (picked at random), by a fifth of the hull damage
  (`rules.moduleDamage`; a half broke the Lancer's only gun too soon).
  A module at 0 is broken: it stops working until it is repaired at the
  colony. Internal modules are hurt only by the tesla (later).

## Ships

| Stat | Unit | What it does |
| --- | --- | --- |
| reactor | MW | power; what modules leave unused refills the capacitor |
| mass | t | the empty hull; modules and cargo add to it |
| maxSpeed | m/s | top speed |
| thrustForward, thrustBackward | kN | acceleration = thrust ÷ total mass |
| thrustTurn | kN | turn rate (rad/s) = turn thrust ÷ total mass |
| hull | HP | at 0 the ship is destroyed |
| shield | HP | takes hits first (see damage) |
| shieldRegen | HP/s | passive, always on |
| shieldThreshold | HP | kinetic and thermal per hit |
| slots | count | external, internal, rigs |
| capacitor | GJ | energy for active modules and shots |
| resist | share | EM, explosive, kinetic, thermal |
| scanner | m | how far the ship sees: its turrets pick targets within it |
| cargo | m³ | ammo, loot |
| cargoMassFactor | × | cargo mass × this is added to the ship's mass |

Total mass = hull mass + modules' mass + cargo mass × cargo mass factor. A
full hold makes a ship slower to accelerate and turn, but never slower at
top speed.

## Power and the capacitor

- Each fitted module draws **passive power** (MW). Active modules also
  draw **energy** while working (GJ/s), or per shot (GJ).
- Each second the capacitor gains (reactor − passive power) ÷ 1000 GJ and
  loses what active modules and shots use. It stays between 0 and its size.
- A module that needs energy the capacitor lacks waits: a laser goes dark,
  a shot waits.
- **Over the reactor** (more passive power than the reactor gives): the
  capacitor drains. While it is empty, modules go offline from the last
  slot back until the passive load fits; they come back when it is half
  full again. The fitting screen warns about such a fit, but allows it.

## Weapons

All the developer's weapons, for later; the MVP has the laser and plasma.

| Weapon | Fire | DPS | Range | Projectile speed | Ammo cost | Turret cost | Turret turn | Damage |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Cannon | low rate | low | high | high | moderate | low | moderate | kinetic |
| Gatling | very high rate | high | very low, cone | instant | moderate | moderate | moderate | kinetic |
| Plasma | high rate | very high | moderate | moderate | very low | high | high | kinetic + thermal |
| Laser | channeled | moderate | moderate | instant | none | high | moderate | EM |
| Tesla | channeled, then one release | moderate | low | instant | none | very high | none: hits the nearest | EM |
| Missile launcher | moderate rate | moderate | high | low | high | low | moderate | explosive |

Missile launchers fire **missiles** (faster, dearer, shorter-lived, less
damage, steered) or **torpedoes** (slower, cheaper, longer-lived, more
damage, not steered).

Turrets turn toward their target at their turn speed and fire on their own
once on it: within 3° for beams, and at the lead point for shots. They are
not drawn on ships (the developer, 2026-10-04): the player's own ship shows
its turret's aim in the HUD, an arc around the ship with a pointer (blue
when ready, amber while reloading, dim when offline or broken); the others'
turrets show only by what they fire.

## The MVP's content (`content/catalog.json`)

The starting values, after the first tuning of 2026-10-04 (turrets must
out-turn hulls, or beams slip off target when a pilot turns hard; slow,
loose-aimed shots missed agile frigates). The catalog has the current ones.

**Lancer**, frigate (S): reactor 100 MW, mass 120 t, top speed 40 m/s,
thrust 3600 / 1440 kN, turn thrust 260 (about 110°/s fitted); hull 400, shield 300 (regen 6/s,
threshold 8); capacitor 2 GJ; slots 1 external, 1 internal, 0 rigs;
resistances EM 0, explosive 10%, kinetic 20%, thermal 10%; scanner 250 m;
cargo 20 m³ at mass factor 1.

| Module | Slot | HP | Mass | Power | Energy | What it does |
| --- | --- | --- | --- | --- | --- | --- |
| Pulse laser S | external | 60 | 6 t | 15 MW | 0.04 GJ/s while firing | 24 EM DPS, 100 m, turns 2.5 rad/s |
| Plasma gun S | external | 60 | 9 t | 25 MW | 0.006 GJ a shot | 3 shots/s, a 0.3 m³ magazine (30 charges), 4 s reload, kinetic ×1.0, thermal ×1.2, 100 m, shots at 180 m/s, fires within 1° of its lead, turns 3.5 rad/s |
| Shield booster S | internal | 40 | 4 t | 5 MW | 0.05 GJ/s while on | +15 shield/s; on while the shield is below full and the capacitor above 20% |
| Capacitor battery S | internal | 40 | 10 t | 0 | none | +1.5 GJ capacitor |

**Plasma charge S** (ammo): 0.01 m³, 0.01 t, 5 kinetic + 5 thermal a
shot (×1.0 and ×1.2 in the gun: 11 a shot, 33 DPS).

The choice in the one external slot: the laser is moderate, endless and
EM (it hits shields hard), but costs capacitor; plasma hits harder and
bleeds through the shield's threshold, but needs ammo. Against a Lancer, a
plasma shot of 11 becomes 9.4 after resistances: 8 to the shield and 1.4
to the hull.

**Enemies** in the MVP are pirate hulls from the same catalog: the
**Raider** (hull 120, shield 70, threshold 5, top speed 34 m/s, one
external slot: a laser or a plasma gun), with strength and numbers rising
by ring (below). A Lancer that holds still clears ring 1's two raiders in
about 35 s and keeps most of its hull.

## Orders (indirect control)

Decided 2026-10-04: orders, automatic turrets, and the stick still steers.

- **Tap or click empty space:** the ship flies there and stops.
- **Tap or click an enemy:** it becomes the target; the ship closes to 80%
  of its weapon's range and circles it there. Turrets prefer the target.
- **No order:** the ship slows to a stop; turrets fire at the nearest enemy
  in range.
- **Keys, the stick or a held finger** steer the ship directly and cancel
  a move order, but not the target.
- **The right button** stops, and drops the target.
- Other ships of the fleet follow the flagship and attack its target.
- **The view** rises to show the enemies and crates within 160 m (weapons
  reach 100 m, ships are 3 m long); the wheel zooms. Seen from far, every
  ship has a ring in its side's color, and crates a gold one; marks at the
  screen's edge point to those off screen.

## Battles

- **Launch:** 1 command point, and He-3 fuel per ship: 5 + 1 per ring. Up
  to 3 ships (the flagship and two escorts).
- Ships start with the hull, modules and cargo they came home with. Repairs
  happen at the colony.
- **The field:** an asteroid field from the node's seed: 250 rocks in a
  500 m square, 50 m clear at the start (with 500, rocks took 42% of the
  shots). Enemies come in a group, in numbers and strength by ring:
  - ring r: 2 + ⌊r/2⌋ raiders, at most 12;
  - their hull, shield and damage × 1.12^(r−1).
- **Loot:** each destroyed enemy drops a crate: metal, He-3 and chips by
  ring, and now and then a module or ammo. A ship picks up a crate by
  passing within 6 m of it, if it has room in its hold. Crates last 90 s.
- **The end:**
  - all enemies destroyed: "Area cleared". The node counts as cleared, the
    fog moves back, and the fleet returns when the player says so, or by
    itself after 60 s;
  - "Return" at any time: the fleet leaves with what it carries, and the
    node stays as it was;
  - every ship destroyed: the battle is lost, with the ships.

Resources in the holds go to the colony's storage. Loot may fill storage
past its cap; only production stops at the cap.

## The colony

Every building starts at level 1. Each building upgrades on its own: all
five can be upgraded at the same time (the developer, 2026-10-04), and the
same one only once at a time.

| Building | Gives at level 1 | Each level |
| --- | --- | --- |
| Metal mine | 120 metal/h | ×1.5 |
| He-3 extractor | 60 He-3/h | ×1.5 |
| Chip fab | 30 chips/h | ×1.5 |
| Depot | storage for 1000 of each | ×1.6 |
| Command center | 10 command points at most, 1 back every 12 min | +2 points, refill ×0.9 time |

- An upgrade costs metal and chips (a level's base cost × 1.7^(level−1))
  and takes time (30 s × 2^(level−1) while we test; real times later).
- Production is counted by the server from the clock: the colony grows
  while the game is closed.
- Start: 600 metal, 200 He-3, 100 chips; a Lancer fitted with a plasma gun
  and a shield booster, 300 plasma charges in its hold; in storage a pulse
  laser, a capacitor battery and 300 plasma charges; blueprints for all of
  them.
- With no ship left and too little metal to build one, the colony gives a
  basic Lancer, so that the loop never locks.

## Hangar, inventory and blueprints

- **Inventory** (the "backpack"): what the colony stores: resources,
  modules (each with its condition), ammo, and the blueprints the account
  knows.
- **Hangar:** the account's ships, each with its fit (a module per slot)
  and its hold (ammo, loot). Fitting moves a module between storage and a
  slot; loading moves ammo between storage and the hold. The ship's stats
  (power, capacitor balance, mass, acceleration, DPS) update as the fit
  changes.
- **Blueprints** build ships, modules and ammo from resources. Building is
  instant in the MVP. Blueprint tiers (T1, T2, …) come later.
- **Repairs** cost metal per hit point, for the hull and modules.
- **The screens** (2026-10-05, ADR 0017), as EVE's windows:
  - the Inventory tab: the places (the colony's storage, each ship's hold
    and fitting, the blueprints) and their items as tiles, each module
    its own; dragging moves them: charges into a hold (as many as fit)
    and back, modules onto a ship or a slot and back;
  - the Fitting tab: the ship in 3D inside a ring of its slots (externals
    over it, internals on its right, rigs under it), the modules that fit
    beside it, its numbers in sections; drag or double-click to fit and
    to take out; the repair.

## The star map

Decided 2026-10-04: rings outward from the colony.

- Hexes around home: ring n has 6n nodes, forever.
- A node can be attacked when it touches home or a cleared node.
- **Fog:** only nodes within two steps of home or a cleared node show.
  At the start, rings 1 and 2.
- Each node shows its ring, its kind and what it may hold: **pirates**
  (the usual), **rich** (more enemies, more loot); more kinds later.
- One map seed for everyone (a season), so that the leaderboard compares
  the same map.
- A cleared node can be fought again for its loot. The leaderboard counts
  only the deepest ring.

## Not in the MVP

Rigs; size M and up; cannon, gatling, tesla and missiles; ship and module
tiers; building times for ships; the tutorial's place in the loop; fleets
of more than three; PvP; the interstellar chest and how this loop's
progress meets the battle and war modes' (`docs/vision.md`'s progression
table was written for those).

## The colony's scene

The Colony tab shows the colony as a 3D scene above its five cards
(`client/src/colony_view.cpp`; the developer chose one scene over a model
on each card, 2026-10-04), laid out as in the colony concept they picked
(SCN1, `content/generated/concepts/colony_scene.jpg`): the buildings in a
cluster on octagonal pads, roads from a junction to each, on a pale gray
moon with the gas giant rising behind the horizon, the camera swaying
slowly. A building grows a little with each level (to level 7), glows gold
while it is upgraded, and lights up under the pointer; a click picks it
and marks its card. The scene names only the building hovered or picked,
and those being upgraded (with their time left): the cards name them all.
Kenney's Space Kit models stand in until the generated ones
(`docs/content-generation.md`).
