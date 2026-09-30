# 0003. Netcode model: snapshots, own-ship prediction, lag compensation

- Status: Proposed
- Date: 2026-09-30

## Context

The server has authority. Battles can reach ~500 ships and ~5000 fast
projectiles. Clients range from a PC player flying one ship directly to a
phone player giving fleet orders. Web clients may only have a TCP-like
transport. The transport itself is an engine decision (see pith ADRs).

## Decision

- **Fixed tick.** The sim runs at a fixed tick rate, a single constant.
  Start at 30 Hz; choose the final rate with the 500-ship stress test
  (30 vs 60 Hz). Rendering interpolates between ticks at any refresh rate.
- **Snapshots.** The server sends delta-compressed snapshots, filtered by
  interest (what the client can see or detect). Strategic (phone) clients
  get lower rates and less detail.
- **Interpolation.** Clients show remote objects slightly in the past
  (about 100 ms) and interpolate between snapshots.
- **Prediction.** A client predicts only the ship it flies directly, and
  corrects itself when the server disagrees. Ships under orders or bots are
  not predicted.
- **Projectiles as events.** Projectiles are not replicated one by one. The
  server sends fire events (shooter, weapon, tick, origin, direction, random
  seed); clients simulate the flight for visuals. Only the server decides
  hits and damage and sends them as events.
- **Lag compensation.** The server keeps a short position history (about
  250–500 ms, cheap thanks to ADR 0001) and checks fast direct-fire weapons
  against what the shooter saw. The rewind is capped to limit "hit behind
  cover" complaints.
- **No full rollback netcode** for battles: re-simulating 500 ships per
  correction is too expensive, and it needs strict determinism. Revisit for
  small PvP skirmishes if prediction plus lag compensation is not enough.
- **Determinism** is not needed for correctness. Keep the sim deterministic
  for the same build on the same OS where it is cheap (fixed tick,
  seeded random numbers, no uninitialized state): it helps tests, bots and
  debugging. Bit-exact results across operating systems are not a goal.

## Consequences

- Bandwidth scales with ships and events, not with projectiles.
- Clients may briefly show a projectile hitting when the server says it
  missed, or the other way round. Visual effects must tolerate a hit being
  confirmed late.
- The history buffer and interest management need to be in place before
  online PvP.
