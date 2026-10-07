# Architecture decision records

One file per significant decision: context, decision, consequences.
Statuses: Proposed → Accepted, or Rejected; Deferred keeps a decision as
the intended direction without building or settling it yet; an old
decision can become Superseded by a newer one. Only the developer accepts,
rejects or defers an ADR (the first pass: 2026-10-03). Never rewrite an
accepted ADR; add a new one.
Engine decisions live in `../pith/docs/adr/`.

| # | Decision | Status |
| --- | --- | --- |
| 0001 | [Simulation data model: typed pools, no ECS library](0001-sim-data-model.md) | Accepted |
| 0002 | [Only our dedicated servers grant persistent rewards](0002-reward-trust-boundary.md) | Accepted |
| 0003 | [Netcode model: snapshots, own-ship prediction, lag compensation](0003-netcode-model.md) | Accepted |
| 0004 | [Match history as an event log; finishers from a client buffer](0004-match-history.md) | Deferred |
| 0005 | [Backend services: brainCloud behind our own interface](0005-backend-services.md) | Proposed; a spike first |
| 0006 | [The client: pith's shell, screens, text from string tables](0006-client-structure.md) | Accepted |
| 0007 | [The local server: the same server code over loopback](0007-local-server.md) | Accepted |
| 0008 | [Combat v0: circles, a gun, an asteroid field in the sim](0008-combat-v0.md) | Accepted (v0) |
| 0009 | [Bots v0: enemy pilots, waves, ships that take damage](0009-bots-v0.md) | Accepted, amended |
| 0010 | [The online server: stellanova-server on the developer's machine](0010-online-server.md) | Accepted (test phase) |
| 0011 | [Chat and commands: lines for everyone, bots on request](0011-chat-and-commands.md) | Accepted |
| 0012 | [Own-ship prediction, protocol 4 and a harder server](0012-prediction-and-protocol-4.md) | Accepted |
| 0013 | [Ships, modules and damage from a catalog; turrets and orders](0013-ships-modules-damage.md) | Proposed |
| 0014 | [Accounts and the solo loop on our game server, until a backend](0014-accounts-on-the-game-server.md) | Proposed |
| 0015 | [The game's website: a page about the game, and the game apart](0015-website.md) | Proposed |
| 0016 | [Sign-in with Steam; guests unsaved; the admin page](0016-sign-in-and-admin.md) | Proposed |
| 0017 | [The inventory: every owned thing as an item, moved by dragging](0017-inventory.md) | Proposed |
| 0018 | [One interface for every screen and every way of playing](0018-one-interface-every-device.md) | Proposed; the playground built |
