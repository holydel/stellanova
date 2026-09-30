# 0004. Match history as an event log; finishers from a client buffer

- Status: Proposed
- Date: 2026-09-30

## Context

The game wants a match history ("ship A was at X and took N damage from
ship B's laser") and, as a nice-to-have, slow-motion finishers that replay
the last 3–5 seconds before a ship explodes. A full per-projectile replay is
not needed.

## Decision

- **Match history (server):** a compact log with two parts:
  - position samples of every ship at a low rate (2–4 Hz, quantized);
  - discrete events: spawn, order changes, damage (attacker, target,
    weapon, amount), destruction, captures, base damage.
  About 20 MB raw for a 20-minute, 500-ship battle, much less compressed.
- **Finishers (client):** each client keeps a ring buffer of the last ~5
  seconds of snapshots and fire events it received, and replays a
  destruction from it in slow motion with the cinematic camera. The server
  does nothing extra.

## Consequences

- History files are small enough to store, upload and analyze with tools
  or an AI (balance questions, cheat reports, bug reports).
- A history cannot re-render the exact fight; finishers only exist live on
  the client that saw them.
