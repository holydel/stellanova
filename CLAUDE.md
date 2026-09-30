# Stella Nova (codename `stellanova`)

Sci-fi space war game, free-to-play: 2D gameplay, 3D graphics. Top-down
camera in gameplay; the camera moves freely for cutscenes and finishers.
Private repo, never public. Built on the `pith` engine, checked out as a
sibling at `../pith` (see `../pith/CLAUDE.md` for engine rules). Never
commit API keys. Game design: `docs/vision.md`. Plan: `docs/roadmap.md`.

## Design principles

pith's, for the game code too:

@../pith/docs/design-principles.md

## Targets

Terms follow pith's glossary (`../pith/CLAUDE.md`): OS, platform (store),
device.

- OS: Windows, Linux (Steam Deck, Steam Frame), macOS/iOS, Android (phones,
  Quest, Android XR), with a VR mode on XR headsets, and a runnable web
  build. (UWP is for engine demos only, not the game.)
- Platforms: Steam, Meta, Google Play, Apple. The web has none: web login
  and purchases are decided later.

## Architecture

- Client-server, server-authoritative.
- The game simulation is one shared library (`sim/`), used by the client,
  the dedicated server, and headless bots/tests.
- Local games (offline, tutorial): the server is statically linked into the
  client and talks to it over the engine's in-process loopback transport,
  so local and online games run exactly the same logic.
- Online games: the same server code runs as `stellanova-server` (Linux).
- Backend services (accounts, inventory, purchases, persistent state) are
  third-party only: no self-written backend.
- Minimal dependencies: the goal is 2 seconds from tapping the icon to a
  starfield with a ship.

## Conventions

- Namespace `sn::`, macro prefix `SN_`.
- Executables: `stellanova` (client), `stellanova-server`.
- Layout: `sim/` `client/` `server/` `bots/` `content/` `docs/`.
- Record significant decisions as ADRs in `docs/adr/`.

## Game (short)

- Modes: skirmish (fleet vs fleet, rising tiers), battle (a MOBA-like
  round in one star system), war (the same with several star systems, over
  weeks). Opponents: bots, players, or both. Details in `docs/vision.md`.
- Start with solo and offline; the tutorial must work without internet.
- Release order: 1) offline core, 2) online and co-op, 3) competitive PvP.

## Workflow

- Commit only when the developer asks; never push. When a change is ready,
  summarize it and suggest a commit message.
