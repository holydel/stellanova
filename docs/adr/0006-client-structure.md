# 0006. The client: pith's shell, screens, text from string tables

- Status: Accepted (2026-10-03, by the developer)
- Date: 2026-10-01

## Context

- The developer asked, on 2026-10-01, to start the game itself, with a menu
  for example, once the engine had text, input, Steam and audio.
- CLAUDE.md: `sim/` shared by the client, the server, bots and tests;
  English at launch with no text in the code (AI-assisted translation
  later); minimal dependencies; 2 seconds to a starfield with a ship.
- pith ADR 0030 turned the samples' frame code into `ph::shell`, which a
  game can use.

## Decision

- **Build:** the game is its own CMake project that includes pith's
  `cmake/ph_*.cmake` and adds `../pith` as a subdirectory (`SN_PITH_DIR`),
  without pith's samples, tests and tools. Shaders and packs come from the
  pith tool of pith's own Windows build, which `scripts/build.ps1` builds
  first. Presets: Windows (MSVC, clang-cl), the web, Linux (the Deck); the
  APK comes from pith's Gradle project with the game's folder as its root
  (`scripts/build.ps1 android`; the package `stellanova.game` is a
  placeholder).
- **`stellanova`** is one executable per OS on the backend it ships with
  (Vulkan; WebGPU on the web), on `ph::shell` with `devControls` off: the
  game owns Escape and Back, the window is titled "Stella Nova", and Steam
  runs as app 1096260.
- **Screens** are plain classes the app switches between: `Menu` (the main
  page and the settings page) and `Flight`. Each takes events and the
  frame, draws its 3D part, then its interface on pith's canvas in the same
  pass. No UI library yet (roadmap M1.5 chooses one).
- **The interface** is laid out in units of a 720-unit-tall screen, so it
  scales with the window; margins, the title's size and wrapped lines adapt
  to narrow screens (phones, a square browser window).
- **Input:** menus take the keyboard, a gamepad's d-pad and left stick, and
  the mouse; a finger works as the mouse (pith ADR 0027). Flight takes the
  keys, the left stick and triggers, and a finger or the held mouse as the
  point to fly to.
- **Text:** every word on screen is a key in `content/strings/en.txt`
  (`key = text` lines, `{}` for a number), read from the pack into a
  `StringTable`. A translation is the same file in another language.
- **Settings** (volumes, fullscreen) live in `settings.txt` in the app's
  data folder, as `key = value` lines, saved when they change.
- **Simulation:** `sim::World` holds fixed pools (ADR 0001) and steps at 30
  Hz (ADR 0003) in the server (ADR 0007); `Flight` keeps the last two
  snapshots and draws between them.

## Consequences

- The menu and flight run on Windows, in Chrome and on the Steam Deck; the
  starfield shows 0.8 s after start on the dev PC (Dev build) and 0.14 s on
  the Deck.
- Strings and settings share one small `key = value` reader.
- Placeholder models are Kenney's Space Kit (CC0), baked from glTF into the
  pack (pith ADR 0031), until the content pipeline (M1.7) makes our own.
