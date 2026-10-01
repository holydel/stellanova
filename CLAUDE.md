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

## Build and run

- `scripts/build.ps1 <preset> [-Config Dev] [-Target stellanova]`: presets
  `windows-msvc`, `windows-clang`, `web`, `linux-x64` (`CMakePresets.json`),
  and `android` (an APK through pith's Gradle project, `-Pph.root`; package
  and label in `scripts/android.ps1`).
  pith builds as a subdirectory (`SN_PITH_DIR`, default `../pith`) without
  its samples, tests and tools; the pith tool of pith's own `windows-msvc`
  Dev build makes the shaders and packs, and the script builds it first.
- `scripts/run.ps1 [-On win32|web|deck|android] [-Arguments '...']
  [-Screenshot shot.png]` builds and runs the game and prints its log. Options: pith's
  shell options (`ph/shell/shell.h`: `--frames`, `--screenshot`,
  `--platform none`...), `--flight` (start flying) and `--mute`.
- Tests: `ctest --test-dir build/<preset> -C Dev` (`sn_tests`, smoke runs of
  the menu and of flight, and the menu's screenshot test on Windows);
  `scripts/check.ps1` checks formatting, builds every preset in every
  configuration and runs the tests (the Deck's over SSH). Run it before
  every commit.
- Code: `sim/` (`sn::sim`: the simulation and the protocol), `server/`
  (`sn::server`: the match's authority; a local game runs it in the client
  over pith's loopback), `client/` (`stellanova`: menu, settings, flight),
  `content/` (the pack manifest, the strings tables, the pinned sources and
  their credits).
- Steam: the game runs as app 1096260 (`client/src/main.cpp`); pith's
  samples use 480.

## Game (short)

- Modes: skirmish (fleet vs fleet, rising tiers), battle (a MOBA-like
  round in one star system), war (the same with several star systems, over
  weeks). Opponents: bots, players, or both. Details in `docs/vision.md`.
- Start with solo and offline; the tutorial must work without internet.
- Release order: 1) offline core, 2) online and co-op, 3) competitive PvP.

## Workflow

- Commit only when the developer asks; never push. When a change is ready,
  summarize it and suggest a commit message.
