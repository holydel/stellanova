# Stella Nova: game vision

Draft from the Q&A of 2026-09-30. Update it when a decision changes.
Lines marked *(interpretation)* are my reading of the answers; confirm or fix them.

## Pitch

A sci-fi space war game. Each player commands a small fleet (4–7 ships) and
fights alongside or against other players and bots. It sits somewhere between
Whiteout Survival (F2P meta, big spenders), EVE Online (fleets, territory
war) and Path of Exile (random builds, deep item progression).

## Modes

| Mode | Length | What happens |
| --- | --- | --- |
| Skirmish | shortest | Your fleet (1–5 ships) vs an enemy fleet in empty space or an asteroid field. After a win, choose the next, harder tier. |
| Battle | one round | A MOBA-like round in one solar system. Two teams start at their bases; the goal is to destroy the enemy base. Attack early, or gather resources to be strong later. |
| War | several weeks | The long version of a battle: several star systems linked by gates, but still only two bases, so it is one big, long MOBA session. Teams fight for control of systems to get an advantage in the final fight. Asynchronous: players can act from a phone. |

Every mode can be played against bots, players, or both: solo vs bots,
players vs bots (co-op), players vs players. Examples for skirmish: 1 player
vs bots, 2 players vs 2 bots, 2 vs 2.

## Maps

- Skirmish: empty space or an asteroid field.
- Battle: a solar system: 1 sun, N planets, M moons per planet, K asteroids.
  Objects are grouped into points of interest; winning a fight there gives
  a benefit.
- War: several solar systems connected by gates.

## Control layers

From lowest to highest:

1. Direct control: rotate and thrust.
2. Auto modes: move to a point, hold an area, patrol a path segment.
3. Hand the ship to a bot: gather resources, defend the base, attack the
   enemy base.

A player can fly one ship directly while bots run the others, or command
the whole fleet from above. The first 30 seconds are a tutorial for direct
control.

## Asymmetric cross-platform play

- PC (and gamepad devices): direct control is convenient.
- Phone: strategic control: decide where ships go and what they do (layers
  2–3). Phones are always with the player, so they suit asynchronous war
  decisions.
- Players on different devices play in the same match.

## Progression

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
- *(interpretation)* Resources earned in a battle disappear when it ends;
  only chest slots and account progress remain.

## Scale

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
- Budget: fixed monthly time and money; target about one year, not a hard
  deadline. $1000 for AI tools (models, music, sounds).

## Technology (game side)

- C++ only; no scripting language. Hot reload: maybe later.
- Minimal dependencies. Goal: 2 seconds from tapping the icon to a starfield
  with a ship.
- English at launch; plan for AI-assisted translation (no hard-coded
  strings).
- Content pipeline: to be found by trying third-party AI tools for models,
  images, sounds and music, and keeping what works.

## Open questions

1. Offline rewards. Proposal (ADR 0002): matches on the local (loopback)
   server give no persistent rewards, because a player's device can be
   tampered with. Only our dedicated servers can grant rewards.
2. Team sizes for battle mode.
3. When does the war mode come: after PvP, or earlier as a PvE mode?
4. Ship shapes for collision: circles and boxes, or detailed hulls?
5. VR gameplay design (after the engine's XR layer exists).
6. Web login and account linking.
