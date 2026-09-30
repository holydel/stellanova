# Roadmap (pith + Stella Nova)

A living plan. Only the next milestone is detailed; later ones get detail
when we reach them. One iteration is about one hour of focused work that
ends in a state worth committing. A milestone is a group of iterations with
a clear "done when".

Engine work is mostly pulled by the game: pith gets a feature when the game
needs it, and stays generic (no game content in pith). Two exceptions are
engine goals of their own: the scene graph and the XR layer.

## Phase 0: engine foundation (pith)

### M0.1 Skeleton

- CMake presets per platform; `core` (types, log, assert, time, arenas);
  doctest; a local `check` script.
- Done when: `check` builds and runs the tests on Windows.

### M0.2 Window and loop

- Platform layer with a `tick()`-driven loop: Win32 first, then Linux,
  Android, Apple, web. Startup timer from process start to first frame.
- Done when: an empty window runs on Windows and Linux (Steam Deck).

### M0.3 Triangle

- RHI with the Vulkan and Null backends; Slang compiled at build time;
  offscreen screenshot and compare tool.
- Done when: the triangle screenshot test passes on Windows.

### M0.4 Triangle everywhere

- Metal (Mac, iPhone), WebGPU (browsers, Dawn native), Android (Pixel,
  Quest in flat mode, Galaxy XR), Linux (Steam Deck).
- Done when: the screenshot test passes on every device, and startup time
  is recorded for each.

### M0.5 Scene graph

- ADR for a state-of-the-art, data-oriented scene graph with hierarchy;
  implementation; a stress sample with many animated nodes.
- Done when: the sample runs on every platform, with a recorded benchmark.

## Phase 1: offline core

- **M1.1 Starfield and a ship.** Baked mesh format v0 (glTF in, runtime
  layout out); `sim/` with a fixed tick; loopback server; direct control
  (keyboard, mouse, gamepad, touch). Measure "1 second to starfield".
- **M1.2 XR layer (pith).** OpenXR session, multiview stereo, 6DoF
  controllers, haptics on Quest, Galaxy XR and SteamVR. Sample: the
  starfield and ship on a virtual table.
- **M1.3 Combat v0.** Weapons, projectiles, damage, destruction; 2D
  collision (own shapes vs Box2D: ADR); simple attack bot.
- **M1.4 Fleet and orders.** 4–7 ships; auto modes (point, area, patrol);
  hand a ship to a bot; the 30-second control tutorial.
- **M1.5 Skirmish loop.** Enemy fleets, tiers, win and lose, results; in-game
  UI library choice (ADR).
- **M1.6 Feel.** Audio, haptics, effects, cinematic camera, finisher
  prototype.
- **M1.7 Content pipeline.** ImGui editor: model viewer, prompt → model →
  bake loop with third-party AI tools; texture compression tool.
- **M1.8 Scale test.** 500 ships and 5000 projectiles offline; profile on a
  phone and on Quest; choose the tick rate.

## Phase 2: online and co-op

- **M2.1 Transports.** Native UDP and browser transport; `stellanova-server`
  on the Linux test server.
- **M2.2 Online skirmish.** Snapshots, interpolation, own-ship prediction;
  co-op vs bots.
- **M2.3 Backend.** Accounts and linking, inventory, server-granted rewards
  (ADR 0002); first store test builds (Steam, Google Play internal testing,
  TestFlight, Meta).
- **M2.4 Battle mode v0.** Solar system map, bases, points of interest,
  resources; co-op vs bots.
- **M2.5 Progression.** Account, character tree, blueprints, chest slots,
  in-app purchases.
- **M2.6 Phone strategy client.** Fleet orders from a phone in the same
  match as PC players.

## Phase 3: competitive and meta

- Lag compensation, matchmaking, regions, basic anti-cheat, ranked PvP.
- War mode: a long battle across several systems linked by gates, with
  asynchronous actions.

## Not yet placed

- VR gameplay (tabletop-like indirect control?): designed after the XR
  layer exists.
- Web login and account linking.
- Hot reload of game code.

## Next iterations

1. pith: CMake skeleton, Windows preset, `core` library, doctest, first
   test, `check` script.
2. pith: logging and asserts (`PH_LOG`, `PH_ASSERT`), high-resolution time.
3. pith: platform layer decision (own vs SDL3: ADR), Win32 window, `tick()`
   loop, empty sample.
4. pith: Vulkan instance, device, swapchain; clear the screen.
5. pith: Slang compile step in CMake.
6. pith: the triangle.
7. pith: offscreen render, PNG screenshot, compare tool.
8. pith: startup time measurement.
9. pith: Linux build on Steam Deck.
10. pith: Android build on Pixel and Quest.
