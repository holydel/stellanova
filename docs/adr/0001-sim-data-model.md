# 0001. Simulation data model: typed pools, no ECS library

- Status: Accepted (2026-10-03, by the developer)
- Date: 2026-09-30

## Context

`sim/` runs on the dedicated server, in the client (prediction and the local
server), in bots and in tests. A battle can hold ~500 ships, ~5000
projectiles, asteroids, planets, moons, bases and gates. The sim needs:

- fast updates of many similar objects;
- a fixed, reproducible update order (tests, replays, desync debugging);
- cheap copies of the state: lag compensation keeps a few hundred ms of
  history, and the network layer needs snapshots and deltas;
- no exceptions, no RTTI, few dependencies.

Options:

1. A generic ECS library (EnTT, flecs).
2. Our own generic ECS (archetypes or sparse sets).
3. A fixed set of entity kinds, each stored in its own pool.

## Decision

Option 3. The game has a small, known set of kinds: ship, projectile,
asteroid, celestial body (sun, planet, moon), base, gate, resource node.

- Each kind has its own pool of plain structs (split into hot and cold
  arrays where profiling asks for it).
- Objects are referenced by handles `{index, generation}`; stale handles
  are detected, never dereferenced.
- Systems are plain functions (`update_ships(World&)`,
  `update_projectiles(World&)`) called in a fixed order each tick.
- Variation inside a kind is data, not new types: a ship is a hull plus
  module slots (weapons, engines, shields) described by blueprint data.
- Queries across kinds ("everything that can take damage near X") go through
  one spatial grid that stores `(kind, handle)` pairs.
- The sim does not use pith's scene graph: the sim must stay independent
  of rendering, since it also runs on the server. The client mirrors sim
  objects into scene nodes (ship → turrets → engine flames) and feeds the
  renderer. How exactly the game uses the scene graph is decided when
  pith's scene graph exists.

## Consequences

- Simple, fast and easy to debug; state is trivially copyable, so history
  buffers and snapshots are a `memcpy` per pool.
- No dependency and no runtime type registry.
- A new kind needs new code (a pool and its update function). Fine for ~10
  kinds; revisit if the count grows past ~15 or if content needs runtime
  composition.
