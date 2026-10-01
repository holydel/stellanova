# 0007. The local server: the same server code over loopback

- Status: Proposed
- Date: 2026-10-01

## Context

- CLAUDE.md: local games (offline, the tutorial) link the server into the
  client and talk to it over the engine's in-process loopback transport,
  so that local and online games run exactly the same logic.
- ADR 0003: the server has authority; it sends snapshots each tick; the
  client draws between them; fire and hits come later as events.
- pith now has `ph::net` with a loopback transport (pith ADR 0032).

## Decision

- **`server/`** (`sn::server::Server`): listens at an address, gives each
  client that says Hello a ship, applies the newest Input from each, runs
  `sim::Step` at the fixed tick, and sends every client a Snapshot after
  each tick. No globals, so a client process can host one.
- **The protocol** (`sim/protocol.h`, shared with bots later): Hello and
  Welcome (reliable), Input (unreliable, every frame: the newest wins) and
  Snapshot (unreliable, each tick: the ships' positions, velocities,
  angles and controls), one type byte and little-endian fields. A
  snapshot carries at most 32 ships for now (1037 bytes, one datagram);
  interest management and deltas come with bigger matches.
- **The client's local game:** a skirmish starts the server at
  `loopback:stellanova`, then connects to it. Each frame the client sends
  its controls, the server updates (in the same process, same frame), and
  the client takes the snapshots and draws between the last two.
- `stellanova-server`, the executable, comes with pith's UDP transport.

## Consequences

- Flight goes through the server now: what the client shows is what a
  remote server would send, minus the network's delay. Prediction of the
  player's own ship (ADR 0003) comes when the delay does.
- Tests run the server and a client in one process (`server_test.cpp`).
