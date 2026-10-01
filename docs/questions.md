# Open questions and device checks

Started during the overnight run of 2026-09-30 to 2026-10-01 and updated
after the device session of 2026-10-01 (roadmap steps 27 and 28) and the
work of that evening (steps 29-33). Each item says what is blocked and what
an answer or a device session would settle. Remove items once answered.

## To check on devices

- **Android, steps 29-33:** no arm64 device was connected from the afternoon
  of 2026-10-01. Not run on Android yet: `hello_audio` (AAudio), the game.
  The game's APK builds (`scripts/build.ps1 android`) but has not run.
- **Gamepads, by hand:** press every button and stick on the Deck's
  controls and on a PC pad in `hello_input`, and check the names and
  directions (pith ADR 0027). Then fly the game with a pad.
- **Audio by ear:** `hello_audio` (positional hum, buses) and the game's
  menu and thruster on the PC, the Deck and Chrome; tests only measure
  levels.

- **Quest 3:** not connected since step 13. Nothing from steps 14 on has
  run on it: sprites, uploads, pacing, devlink.
- **Galaxy XR and Quest capture while worn:** `vulkan_caps` gets no frames
  while the headset sleeps, so `docs/devices/sm-i610.json` still lists only
  the older 27 formats. Wear the headset during
  `scripts/capture-vulkan-caps.ps1`.
- **HDR on the dev PC's display** (step 23): hello_scene picks scRGB there
  (Windows reports HDR on, SDR white 240 nits, peak 456). Screenshots can
  only check the numbers, so please look: are the UI's whites as bright
  as other windows' whites, and do highlights look right with ACES?
  Compare `hello_scene_vulkan` with `--output scrgb`, `--output hdr10` and
  `--output sdr`.
- **HDR on Android:** not wired yet (`Display.isHdr` and the HDR/SDR
  ratio). The Pixel runs SDR unless `--output hdr10` is given. The Deck is
  done (step 27).
- **Screenshot tests beyond Win32** need fixed-size offscreen targets: phone
  windows have the screen's size. The Deck in Game Mode renders 1280x720
  and matched hello_scene's reference by hand (largest difference 11).
- **Step 24 (present timing):** only the Pixel and the Deck have
  `VK_EXT_present_timing`. The Pixel is reachable again; it waits for a
  slot in the plan.

## Decisions for the developer

- **Steam with Stella Nova's app 1096260:** Steam answers
  "ConnectToGlobalUser failed" on the PC and on the Deck, while app 480
  connects: the account needs a license for the unreleased app (in
  Steamworks, grant the developer comp package to it, or use a key). Until
  then the game runs without Steam.
- **The Android package name:** `stellanova.game` is a placeholder
  (`scripts/android.ps1`); Google Play keeps the first one uploaded
  forever. Which should it be?
- **What next?** M1.1 is done (steps 33-35). Next in the plan: the XR
  layer (M1.2, an engine goal), or combat first (M1.3: weapons,
  projectiles, damage; 2D collision, own shapes or Box2D, an ADR). Or the
  UDP transport and `stellanova-server`. Or first: the
  game on Android (the Gradle project for another repository), a credits
  screen (NASA's credit is required before any release), music (none yet;
  the AI tools budget).
- **The menu's look:** placeholders: Kenney's Space Kit craft and meteors
  (CC0), Kenney's sci-fi sounds for the interface, Noto Sans Bold with a
  glow for the title. Keep the layout (items on the left, the ship turning on the
  right), or another idea?

- **The default sky:** NASA's Deep Star Maps 2020 (4096x2048 OpenEXR, a
  36 MB download once per machine; free with credit: "NASA/Goddard Space
  Flight Center Scientific Visualization Studio. Gaia DR2: ESA/Gaia/DPAC").
  Keep it, or pin your own panorama instead? A file that cannot be pinned
  goes in pith's `content/local/` and a manifest names it `local:<file>`.
- **Sky face size:** 512 pixels (6 MB in the pack as RGB9E5) for now. 1024
  looks sharper on stars but takes 24 MB until BC6H and ASTC HDR quarter
  it. Which for the samples?
- **Devlink's port and reach:** 7707 on 127.0.0.1 by default; 0.0.0.0 only
  when asked. Good, or should devlink need a token even on the LAN?
