# 0005. Backend services: brainCloud behind our own interface

- Status: Proposed; a one-day spike checks brainCloud against the developer's backend list (users, accounts, levels, stats) and the open points below before a decision (2026-10-03)
- Date: 2026-09-30

## Context

Backend services are third-party only. The game needs:

- account login and linking for Steam, Google Play, Apple and Meta;
- validation of in-app purchases from those four stores;
- a player inventory (blueprints, chest slots, account progress);
- server-side logic for the multi-week war;
- matchmaking and hosting of dedicated servers in several regions.

SDKs are needed for C++ (client and server) and possibly JavaScript (web).
Vendors changed a lot in 2026:

- Hathora hosting closed in May 2026.
- Unity Multiplay was deprecated in March 2026.
- Beamable was sold to Skillz in February 2026.
- PlayFab's free tier dropped to 1,000 lifetime players in March 2026.

Candidates:

| Service | Strengths | Weak points |
| --- | --- | --- |
| brainCloud | Login for all four stores; purchase validation for Steam, Apple and Google; scheduled cloud scripts; C++ and JS SDKs; built-in Edgegap hosting; $15–50 per month | Meta purchase validation may need our own script |
| PlayFab | Scale, managed servers | No Meta login or purchases; Microsoft is focusing on Xbox; small free tier |
| Nakama (Heroic Cloud) | Best server-side logic (Go, TypeScript, Lua) | Price only by quote; Steam and Meta purchases are do-it-yourself |
| Epic Online Services | Free, widest account linking | No real inventory database; only an add-on |

## Decision

- Use **brainCloud** for accounts, inventory, purchases, matchmaking and
  war state. Host dedicated servers on **Edgegap** through its integration.
- The game only talks to an `sn::backend` interface. brainCloud is one
  implementation, and a local fake exists for tests and offline play.
  Switching vendors then touches one module.
- Only server-side code changes inventory or war state (ADR 0002). Game
  servers use brainCloud's server-to-server API.
- Nothing is integrated before Phase 2 (online). Until then, only the
  interface and the fake exist.
- Web: no extra third-party login or billing system. How web players log
  in is decided later; until then the web build is playable without an
  account.

## Open points to verify before accepting

- Has brainCloud shipped Meta Horizon purchase validation? If not, can its
  cloud scripts call Meta's server API (outbound HTTP)?
- Real limits and costs at the expected player counts.
- Edgegap's DDoS protection in the regions we need.

## Consequences

- Low fixed monthly cost during development (free up to 100 daily active
  users).
- One more vendor dependency, but it stays behind our interface.
