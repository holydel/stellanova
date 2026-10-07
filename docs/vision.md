# Stella Nova: game vision

Draft from the Q&A of 2026-09-30, updated with the developer's answers of
2026-10-03 (`docs/reviews/2026-10-03.md`). Update it when a decision changes.
Lines marked *(interpretation)* are my reading of the answers; confirm or fix them.

## Pitch

A sci-fi space war game. Each player commands a fleet that grows with the
account, from one ship at the start, and fights alongside or against other
players and bots. Battles are seen at three scales: operational, tactical
and strategic. Ship blueprints are collected and slowly improved, like the
cards of a collectible card game. It sits somewhere between Whiteout
Survival (F2P meta, big spenders), EVE Online (fleets, territory war) and
Path of Exile (random builds, deep item progression).

## Modes

| Mode | Length | What happens |
| --- | --- | --- |
| Skirmish | shortest | Your fleet (one ship at first, more as the account grows) vs an enemy fleet in empty space or an asteroid field. After a win, choose the next, harder tier. |
| Battle | one round | A MOBA-like round in one solar system. Two teams start at their bases; the goal is to destroy the enemy base. Attack early, or gather resources to be strong later. |
| War | several weeks | The long version of a battle: several star systems linked by gates, but still only two bases, so it is one big, long MOBA session. Teams fight for control of systems to get an advantage in the final fight. Asynchronous: players can act from a phone. |

Every mode can be played against bots, players, or both: solo vs bots,
players vs bots (co-op), players vs players. Examples for skirmish: 1 player
vs bots, 2 players vs 2 bots, 2 vs 2.

## The solo loop (the first playable game)

The developer's design of 2026-10-04: a colony that grows while the game
is closed, an endless star map of hex rings around it, battles with fitted
ships for loot, and a leaderboard by the deepest ring cleared. It is the
skirmish mode's solo form: the map's nodes are its rising tiers. Played on
our server even alone, since only our servers grant progress (ADR 0002).
Details and numbers: `docs/solo-loop.md`; ships, modules and damage: ADR
0013; accounts on our server until a backend: ADR 0014; the website: ADR
0015.

*(interpretation)* The progression table below (resources lost after a
battle, the interstellar chest) was written for battles and wars; the solo
loop keeps its colony, ships and blueprints. How the two meet is open.

## Maps

- Skirmish: empty space or an asteroid field.
- Battle: a solar system: 1 sun, N planets, M moons per planet, K asteroids.
  Objects are grouped into points of interest; winning a fight there gives
  a benefit.
- War: several solar systems connected by gates.

## Fleets

The developer, 2026-10-03:

- A player starts in the tutorial with one ship.
- After a few tutorials (2-10 minutes of play), the account has progressed
  enough to open the second fleet slot: the fleet then has two ships.
- The player still flies one ship directly, the flagship; the others take
  orders: attack others, guard the flagship, support the flagship.
- The largest fleet size is chosen after tests.
- *(interpretation)* The tutorial runs offline, and only our servers grant
  account progress (ADR 0002), so the second slot opens at the first
  online login.

## Three scales

The developer, 2026-10-03: the main idea is a game at three scales.

- **Operational:** our ships close up, shown well.
- **Tactical:** less detail, from much farther away.
- **Strategic:** only the map's zones. Orders to a fleet are broad, such
  as "go to zone G12".

So a ship is shown in five ways, from two or three models:

1. the operational model;
2. the tactical model, simpler, for the far view;
3. its mark on the strategic map;
4. the hangar model, seen up close in the hangar;
5. its blueprint.

All five are forms of the ship's design sheet
(`docs/content-generation.md`).

## Control layers

From lowest to highest:

1. Direct control: rotate and thrust.
2. Auto modes: move to a point, hold an area, patrol a path segment.
3. Hand the ship to a bot: gather resources, defend the base, attack the
   enemy base. In a fleet: attack others, guard the flagship, support the
   flagship.

A player can fly one ship directly while bots run the others, or command
the whole fleet from above. The first 30 seconds are a tutorial for direct
control.

## Asymmetric cross-platform play

- Every device can do both: fly a ship directly, or give orders (the
  developer, 2026-10-03).
- PC (and gamepad devices): direct control is convenient.
- Phone: players are expected to prefer orders to the fleet, such as "move
  to this point", maybe choosing by hand when to use abilities that have
  cooldowns. Phones are always with the player, so they suit asynchronous
  war decisions.
- Soon: the indirect "move to a point" order and automatic fire
  (autoshoot), so that phones can play by orders.
- Players on different devices play in the same match.

## Progression

The developer, 2026-10-03: the game is like a collectible card game, with
blueprints as the cards. A player improves them slowly (T1, T2 and on,
without a cap) until they face stronger players, and more of them. Direct
control is fun but probably less effective as a player progresses: progress
means comparing blueprints, fleet setups and fits (what each ship carries),
so the game shows stats, maybe with graphs and diagrams.

| Layer | Speed | Lifetime |
| --- | --- | --- |
| Account | very slow, small stats (e.g. +0.1% damage) | permanent |
| Character | fast, bigger stats; a skill tree that is randomized each time | resets when the session ends: after a battle, or after the whole war |
| Ship blueprints | T1 bought from a vendor, then upgraded to T2, T3, … with no cap; needs a lot of time and resources earned in battles | lost when the battle ends, unless saved |

- **Interstellar chest:** slots that keep a blueprint or a character "DNA"
  (a good character build) after the battle ends.
- **Monetization:** extra chest slots, each more expensive than the last
  (progressive or exponential). This is the main pay-to-win purchase.
- Farming blueprints solo must be very inefficient.
- **Rewards** come only from our dedicated servers: local (offline) matches
  grant none, because a player's device can be tampered with (ADR 0002,
  accepted 2026-10-03).
- *(interpretation)* Resources earned in a battle disappear when it ends;
  only chest slots and account progress remain.
- *(interpretation)* The collection is the blueprints that the interstellar
  chest keeps between battles.

## Battle size

- Target for a battle map: ~500 ships, each with up to ~10 projectiles alive
  (~5000 projectiles).
- If that is too expensive, solve it with design. For example, small
  projectiles do less damage to big targets, so late battles use fewer,
  heavier shots.
- Skirmishes never reach this limit.

## Presentation

- Readable battles matter more than graphics detail.
- Top-down camera in gameplay; a free cinematic camera for cutscenes and
  finishers.
- The interface: today's is fine for an early demo; a good UI system comes
  before the public test (the developer, 2026-10-03). Its screens show
  blueprints as cards, and compare blueprints, fleet setups and fits
  through stats and graphs. The UI system is pith's own (pith ADR 0037,
  2026-10-03): agents write the screens, and every language goes through
  pith's text.
- Finishers (nice to have): a slow-motion replay of a ship's destruction,
  including the 3–5 seconds before it.
- Match history: an event log (where ship A was, "ship A took N damage from
  ship B's laser"), not every projectile.
- VR mode with 6DoF controllers on Quest, Galaxy XR and Steam Frame.
  Probably indirect control, like a tabletop card game. First the engine
  gets a strong XR layer; how the game uses it is designed later.

## Business

- Free-to-play; most revenue expected from big spenders.
- Stores: Steam, Google Play (incl. Android XR), Meta Horizon Store, App
  Store. These stores give a player identity for account linking.
- Web: a runnable build. No extra third-party login or billing systems;
  how web players log in and link accounts is decided later.
- Backend: third-party services only (accounts, inventory, purchases, war
  state). Game servers: own dedicated servers; one test server now,
  several regions later.
- The developer's backend list (2026-10-03): known users and their
  accounts, each account's level and stats; and for the game servers, an
  admin panel: how many players are on, CPU load and traffic, messages to
  all players, and a restart. The vendor is chosen after a one-day spike
  (ADR 0005).
- Budget: fixed monthly time and money; target about one year, not a hard
  deadline. $1000 for AI tools (models, music, sounds); since 2026-10-02,
  up to about $800 a month for generated content. That is a ceiling, not a
  target: spending starts as low as possible, in case the project stops
  (the developer, 2026-10-03; `docs/content-generation.md`).
- First outside players: when the developer thinks the game is ready;
  realistically about January 2027, maybe sooner (2026-10-03).

## Technology (game side)

- C++ only; no scripting language. Hot reload: maybe later.
- Minimal dependencies. Goal: 2 seconds from tapping the icon to a starfield
  with a ship.
- English at launch; plan for AI-assisted translation (no hard-coded
  strings).
- Content pipeline: to be found by trying third-party AI tools for models,
  images, sounds and music, and keeping what works. Since 2026-10-02:
  hosted services through pith's content hub, after a house style
  (`docs/art-style.md`, `docs/content-generation.md`).

## Open questions

1. Team sizes for battle mode.
2. When does the war mode come: after PvP, or earlier as a PvE mode?
3. Ship shapes for collision: circles in v0 (ADR 0008, accepted as v0 on
   2026-10-03); boxes or detailed hulls later, for big ships?
4. VR gameplay design (after the engine's XR layer exists).
5. Web login and account linking.
6. Decided on 2026-10-03: the UI system is pith's own (pith ADR 0037).
7. The backend vendor: brainCloud (ADR 0005) or another, after a one-day
   spike against the developer's backend list. Until then, accounts live in
   files on our server (ADR 0014, the developer's choice of 2026-10-04).
8. How the solo loop's progress (colony, ships, blueprints) meets battles
   and wars, and the interstellar chest.
