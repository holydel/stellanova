# 0008. Combat v0: circles, a gun, an asteroid field in the sim

- Status: Proposed
- Date: 2026-10-01

## Context

- The developer, after flying with an Xbox pad: the left trigger should
  thrust (analog), the left bumper reverse at full power, the left stick
  only turn; the right trigger fires. Rocks should collide (circle with
  circle) and break under fire.
- The roadmap's M1.3 (weapons, projectiles, damage; 2D collision, own
  shapes or Box2D) starts here, in its smallest form.
- ADR 0001: the sim is plain data in fixed pools. ADR 0003: the server has
  authority; fire and hits are events. ADR 0004: the match's history is a
  log of events.

## Decision

- **Everything that collides is a circle**, our own code, no physics
  library: a ship's hull has a radius; rocks and shots are circles.
- **The asteroid field is part of the world** (`sim::Rock`, up to 1024):
  made by the server from a seed (`MakeAsteroidField`: 800 rocks in a
  600 m square, a clearing of 20 m at the start, at least 3 m between
  rocks), and sent to each client whole in Welcome. Clients never make it
  themselves, so it cannot differ between them (float results may differ
  across compilers and CPUs). Rocks do not move.
- **Ships bounce off rocks:** a ship that overlaps one is put back on its
  edge, and its speed into the rock turns back, half of it kept
  (`HullClass::bounce`).
- **A gun per ship** (`sim::WeaponClass`: interval, speed, life, radius,
  damage, as data beside the hull): while `ShipControls::fire` is held,
  a shot from the nose each interval (0.12 s, rounded to ticks), at 90 m/s
  on top of the ship's velocity, for 1.2 s. Shots (a pool of 1024) are
  swept along their way each tick, so a fast shot cannot skip a small
  rock; the first rock on the way takes the damage. A rock's health grows
  with its area (`RockHealth`: 1 hit for the smallest, 15 for the
  biggest); at 0 it breaks.
- **Events** (`sim::Event`, up to 256 a tick): Fired, RockHit,
  RockBroken, ShipBumped, with the ship, the rock, a place and a strength.
  The server sends a tick's events reliably (`Events`), before its
  snapshot; clients play sounds, show sparks and debris, shake the camera,
  and remove broken rocks.
- **The protocol is version 2:** Input carries `fire`; Welcome carries the
  rocks; Snapshot carries the ships, then as many shots as fit 1200 bytes
  (one unreliable message); Events is new.
- **Controls** (the client's `Flight`): pads as the developer asked (the
  trigger's pull is the thrust; the bumper is -1); keys W and S, A and D,
  Space fires; a finger or the mouse held where to go, a second finger or
  the right button fires.

## Consequences

- Tests cover the field (the same for a seed, the clearing, the spacing),
  bouncing, the gun's interval, rocks breaking, the sweep, and the new
  messages, through the server too.
- Not yet: shots hitting ships, ship damage and health, rocks that move,
  split or come back, other weapons; a grid for the rocks (every shot
  checks every rock: fine for a few ships, not for the scale test, M1.8);
  shots in snapshots by interest (ADR 0003); the own ship's prediction.
- The client draws shots a tick behind the newest snapshot, as it draws
  the ships, and moves them by their velocity between snapshots.
