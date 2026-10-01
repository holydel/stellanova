# Architecture decision records

One file per significant decision: context, decision, consequences.
Statuses: Proposed → Accepted, or Rejected; an old decision can become
Superseded by a newer one. Never rewrite an accepted ADR; add a new one.
Engine decisions live in `../pith/docs/adr/`.

| # | Decision | Status |
| --- | --- | --- |
| 0001 | [Simulation data model: typed pools, no ECS library](0001-sim-data-model.md) | Proposed |
| 0002 | [Only our dedicated servers grant persistent rewards](0002-reward-trust-boundary.md) | Proposed |
| 0003 | [Netcode model: snapshots, own-ship prediction, lag compensation](0003-netcode-model.md) | Proposed |
| 0004 | [Match history as an event log; finishers from a client buffer](0004-match-history.md) | Proposed |
| 0005 | [Backend services: brainCloud behind our own interface](0005-backend-services.md) | Proposed |
| 0006 | [The client: pith's shell, screens, text from string tables](0006-client-structure.md) | Proposed |
| 0007 | [The local server: the same server code over loopback](0007-local-server.md) | Proposed |
