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
  third-party only: no self-written backend. One exception until a vendor
  is chosen (ADR 0014): the game server keeps accounts as JSON files.
  Identities come from providers (Steam; browsers' later), which the
  server checks; guests are never saved (ADR 0016).
- Minimal dependencies: the goal is 2 seconds from tapping the icon to a
  starfield with a ship.

## Conventions

- Namespace `sn::`, macro prefix `SN_`.
- Executables: `stellanova` (client), `stellanova-server`.
- Layout: `sim/` `client/` `server/` `bots/` `content/` `site/` `docs/`.
- Every game number is in `content/catalog.json` (ADR 0013), built into the
  sim and read by the website; names are strings-table keys from the ids.
- Sounds: the developer's picks in `content/sounds/` (origins and licenses
  in its README). Most are from Sonniss's GDC bundles: they ship only
  inside the game's packs, never into pith, its samples, any public place or
  a generation tool.
- Icons: one SVG set in `content/icons/` (ids as the catalog's).
  `scripts/icons.py` (needs fontTools) makes the game's icon font, drawn as
  MTSDF glyphs after the text's fonts, and the site's sprite. Run it after
  changing an icon.
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
  `--platform none`...), `--flight` (start flying), `--online` (fly on the
  game's server), `--server ADDRESS` (on another: `udp:host:port`, in
  browsers `ws://`/`wss://`), `--campaign` (the solo loop's hub; online, or
  with `--local` a practice account on this machine), `--tab NAME` (the
  hub's colony, hangar, storage or map), `--inventory-test` (the local hub
  on a seeded account kept in memory, on its inventory: ADR 0017) and
  `--battle` (launch the first ship at once), `--autopilot` (our ship flies itself, as the bots do; local
  skirmish), `--lag MS` and `--loss PERCENT` (a worse network, to try
  prediction: ADR 0012) and `--mute`. A local battle for a picture:
  `--battle --local --frames 2400`. Screenshot runs move time 1/60 s a
  frame: `--frames 600` is 10 s of game time (online, the server keeps
  real time).
- The menus' playground (ADR 0018): `--playground` (the test account's hub,
  a Playground window for the style, the interface's size and jumps to any
  screen, and pith's Device section). pith's `--device
  pc|deck|phone|phone-wide`, `--touch` and `--pad xbox|deck` pretend to be
  another device on Windows: a window of its screen, a finger from the
  mouse, a pad from the keyboard (arrows the d-pad, WASD the stick, Enter A,
  Esc B, Q/E the bumpers).
- Tests: `ctest --test-dir build/<preset> -C Dev` (`sn_tests`, smoke runs of
  the menu and of flight, and the menu's screenshot test on Windows);
  `scripts/check.ps1` checks formatting, builds every preset in every
  configuration and runs the tests (the Deck's over SSH). Run it before
  every commit.
- Code: `sim/` (`sn::sim`: the simulation and the protocol; the catalog,
  fitting, the account's rules and the star map, `docs/solo-loop.md`),
  `bots/` (`sn::bots`: orders turned into controls, and pilots that fly
  ships from what the sim shows), `server/` (`sn::server`: the skirmish and
  battles, the hub's accounts and leaderboard, the admin connection;
  `sn::signin`: sign-ins checked with their providers over HTTPS; a local
  game runs the server in the client over pith's loopback;
  `stellanova-server` runs it alone, over UDP and WebSocket, with `--data`,
  `--leaderboard`, `--signin FILE` and `--admin ADDRESS`), `client/`
  (`stellanova`: menu, settings, the hub's screens on ph::ui (the colony's
  3D scene, the inventory and the fitting screen: ADR 0017), flight),
  `content/` (the catalog, the pack manifest, the strings tables, the
  pinned sources and their credits), `site/` (the game's web page and the
  admin page: ADRs 0015, 0016).
- Steam: the game runs as app 1096260 (`client/src/main.cpp`); pith's
  samples use 480.
- The online server runs on the developer's wos-observer.com machine
  (`docs/adr/0010-online-server.md`), without waves: players chat and call
  bots in (`/help`, `docs/adr/0011-chat-and-commands.md`). Updating it, its
  debug mode, and the web page: `docs/web-test-deploy.md`. Its stats, the client's Network
  window and network captures: `docs/profiling.md`.
- Steam uploads: `scripts/steam-upload.ps1` (`docs/steam.md`).
- Art: the house style is being made from the developer's references in
  `refs/` (git-ignored; never committed, never sent to generators):
  `docs/art-style.md`. Generated content through hosted APIs and pith's
  content hub, with the budget and the first assets:
  `docs/content-generation.md`. Images: `pith gen image` (pith ADR 0041),
  paid from the developer's Google account; models: `pith gen model`
  (Meshy, `MESHY_API_KEY`; 150k triangles and 4k maps, the developer's
  budget). `content/gen.json` holds the limits and keeps `refs/` from ever
  being sent; jobs wait in `.pith/gen` until `pith gen accept`. Accepted
  files go to `content/generated/` (git-ignored for now; their `*.gen.json`
  recipes are committed). The menu's splash and the colony's buildings come
  from there through `content/art.json` (`stellanova-art.pak`, read with
  the main pack; the web's `content/art-web.json` has the splash only).

## Game (short)

- Modes: skirmish (fleet vs fleet, rising tiers), battle (a MOBA-like
  round in one star system), war (the same with several star systems, over
  weeks). Opponents: bots, players, or both. Details in `docs/vision.md`.
- Start with solo and offline; the tutorial must work without internet.
- Release order: 1) offline core, 2) online and co-op, 3) competitive PvP.

## Workflow

- Commit only when the developer asks; never push. When a change is ready,
  summarize it and suggest a commit message.
