# Open questions and device checks

Started during the overnight run of 2026-09-30 to 2026-10-01 and updated
after the device session of 2026-10-01 (roadmap steps 27 and 28) and the
work of that day and evening (steps 29-36). Each item says what is blocked
and what an answer or a device session would settle. Remove items once
answered.

## To check on devices

- **Android, steps 29-36:** not run on Android yet: `hello_audio` (AAudio),
  the game (its APK builds: `scripts/build.ps1 android`). An AGM Glory G1S
  is connected since the evening of 2026-10-01 (Android 11, Snapdragon 480,
  Adreno 619: a low-end phone above pith's floor); it was asleep and
  locked. BlueStacks (`emulator-5554`, API 28) is below pith's minimum.
  Chrome enables WebGPU from Android 12 only, so the web page needs the
  Pixel or a headset.
- **The Steam Deck's controls, by hand** (step 36): `hello_input` on the
  Deck in Game Mode should show "Steam Deck (Steam Deck layout)": A B X Y,
  L1 R1, L2 R2, View and Menu, L3 R3, L4 R4 L5 R5, the quick access button
  ("..."), and both trackpads as 3x3 zones with the finger, its pressure,
  and the click. Check that each lights the right cell, then fly the game
  with them. An external pad on the Deck should show once, as Steam
  Input's, named after the real pad.
- **Gamepads on the PC, by hand:** every button and stick of an Xbox pad in
  `hello_input --platform none` (see the Steam Input item below), then the
  game's new scheme (step 36): left trigger thrust, left bumper reverse,
  left stick turn, right trigger fire.
- **The game in a browser** (https://wos-observer.com/stellanova.html):
  settings reset on reload (the web's data folder is in memory), and
  Settings > Fullscreen may only switch at the next key press (browsers
  allow fullscreen inside an input handler; the game handles keys later).
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

- **Steam with Stella Nova's app 1096260** (`docs/steam.md`): the license
  works since 2026-10-02 (the game is in the developer's library), and
  build 25669850 is uploaded. Until it is live on the default branch,
  Steam serves StarIre's old build, which has no `stellanova.exe`. Left to
  do in Steamworks:
  - SteamPipe > Builds: set build 25669850 live on the default branch;
  - add depot 1096262 (Linux + SteamOS) to the store package: the checklist
    says the store and developer packages differ;
  - Installation > Linux Runtime: Steam Linux Runtime 4.0, which pith
    builds for;
  - depot 1096262 is for the Steam Deck only: "All platforms" would give it
    to Linux desktops and the Steam Frame too. Which?
- **The Android package name:** `stellanova.game` is a placeholder
  (`scripts/android.ps1`); Google Play keeps the first one uploaded
  forever. Which should it be?
- **Steam Input on Windows, for pith's apps (app 480):** when an app
  connects to Steam as Spacewar, Steam Input takes the Xbox pad from XInput
  (`hello_input`: "gamepad 1 disconnected" the moment Steam connects), since
  Spacewar's controls are Steam Input actions. The game, without a license
  for 1096260, never connects, so it keeps the pad. Options: pith reads
  pads through Steam Input's API (an action manifest and a configuration per
  controller type, with the planned input actions, pith ADR 0027), or
  Steam Input turned off for Spacewar in Steam, or `--platform none` for
  input tests (what `hello_input` now suggests). The game's own default in
  Steamworks decides it for 1096260.
- **What next?** M1.1 is done (steps 33-35); M1.3 is nearly done (steps 36
  and 40: rocks, a gun, ships that take damage, enemy bots in waves); the
  online server runs (step 38). Queued: 39 (Steam uploads). Online play
  wants our own ship predicted next: from the dev PC (91 ms round trip) it
  answers about 0.2 s late (ADR 0010). Later: the XR layer (M1.2), fleets
  and orders (M1.4), the game on Android, music (none yet; the AI tools
  budget).
- **The online server's home** (ADR 0010): it runs beside the WOS Observer
  site, sandboxed and capped at 256 MB and half a CPU, and idles at 0.9%
  of one. Anyone with the game can join: no accounts, nothing encrypted.
  Fine for testing; for players, a small machine of its own (and a region
  near them) would keep the site and the game apart. When?
- **The bots' balance** (ADR 0009): bot fighters have 3 shield and 4 hull
  (yours 8 and 12), fly at 16 m/s (yours 40) and fire dodgeable shots at
  35 m/s every 0.6 s, after the halving the developer asked for on
  2026-10-02; waves grow from 2 to 8 bots and aim better each time. The
  autopilot (`--autopilot`) now wins 6 fights a minute without a loss.
  Better? Also: the camera rises to 54 m while enemies are within 70 m;
  enemies off screen show as red marks at the edge.
- **Tile-based GPUs** (pith ADR 0034): two bandwidth savings wait for a
  phone or headset to measure them. Running the scene and the output as two
  subpasses of one pass would keep the HDR scene on chip, about 2.5 GB/s
  less at 60 Hz on a 1080x2400 phone; it needs a subpass step in pith's
  RHI. A B10G11R11 scene target would halve the scene's bytes but bands
  more in dark gradients. Worth doing, and which first? Neither was
  measured: no phone was connected on 2026-10-02.
- **PIX's analysis on the dev PC:** `pixtool save-event-list` fails with
  `E_PIX_ENABLE_EXPERIMENTAL_FEATURES_FAILED` under PIX 2402.07, though
  Developer Mode is on, so D3D12 captures open only in PIX's window. A
  newer PIX should fix it. RenderDoc's path (`pith/scripts/gpu-capture.ps1`)
  works for Vulkan and D3D12.
- **The effects' data on the web** (`docs/profiling.md`): copying the
  frame's effect vertices to the GPU is the web's largest cost that grows
  with the fight (0.28 ms a frame in wave 1). Expanding particles in the
  vertex shader would cut it about 7x. Now, or with the scale test (M1.8)?
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
