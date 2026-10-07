# Open questions and device checks

Started during the overnight run of 2026-09-30 to 2026-10-01 and updated
after the device session of 2026-10-01 (roadmap steps 27 and 28), the work
of that day and evening (steps 29-36), and the developer's answers of
2026-10-03 (`docs/reviews/2026-10-03.md`). Each item says what is blocked
and what an answer or a device session would settle. Remove items once
answered.

## To check on devices

The developer's device weekend (roadmap step 57) is the next chance for
most of these.

- **Android:** the AGM Glory G1S (Android 11, Snapdragon 480, Adreno 619: a
  low-end phone above pith's floor) is connected, with the game installed
  since step 45. No run of the game on Android is recorded yet, nor of
  Android's on-screen keyboard for the chat (ADR 0011) or `hello_audio`
  (AAudio). Which keyboard does the AGM phone use, Gboard or the vendor's?
  The Pixel is back. BlueStacks (`emulator-5554`, API 28) is below pith's
  minimum. Chrome enables WebGPU from Android 12 only, so the web page
  needs the Pixel or a headset.
- **Chat in phone browsers:** the developer (2026-10-03): the chat works in
  desktop browsers (WebGPU) but not in phone browsers. It was never built
  for them (ADR 0011's "not yet"): a phone browser shows its keyboard only
  for a text field. Step 60 adds a hidden one; check it on a phone.
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
  run on it: sprites, uploads, pacing, devlink. The device weekend runs the
  game on it in flat mode. pith ADR 0038 (XR) waits for `hello_xr` on it:
  the developer decides after that run (2026-10-04).
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

- **The solo loop (2026-10-04, `docs/solo-loop.md`)**, open points of the
  first version:
  - **ADRs 0013, 0014, 0015 and 0016** are proposed: accept, change or
    reject.
  - **Steam's Web API key:** Steam sign-in waits for it on the server
    (`docs/web-test-deploy.md`, "Steam's key"); until then every player is
    a guest, and nothing is saved.
  - **Browsers:** Google, Apple and Discord sign-ins are checked by the
    server's code but have no apps yet, and the game page's side is not
    built; a browser player keeps nothing. When? (It must sign in without
    leaving the page, or a guest's colony is gone first.)
  - **Balance:** every number is a first guess in `content/catalog.json`;
    play ring 1 to 3 and say what feels off (ranges of 100 m against 3 m
    ships make the camera rise far; a battle at ring 1 takes about 35 s).
  - **Lost ships** are gone with their modules and cargo; with no ship and
    too little metal, the colony gives a Lancer with a laser. Right?
  - **Times:** upgrades take 30 s doubling each level, and command points
    come back every 12 minutes, for testing. What should they be?
  - **A disconnected battle** fights on under autopilot for two minutes,
    then comes home with what it has (so leaving cannot save a losing
    fleet). Good?
  - **The map's seed** is one for everyone (`map.seed`), so the leaderboard
    compares the same map. A new season: a new seed, and the board starts
    over?

- **The backend (ADR 0005):** Proposed until a one-day spike (roadmap step
  61) checks brainCloud's free tier against the developer's list (Steam
  login, known users, account level, stats) and the ADR's open points.
  Then the developer accepts or rejects the ADR. Meanwhile the game server
  checks Steam sign-ins itself and has an admin page (ADR 0016).
- **Steam** (app 1096260): `docs/steam.md` is the one place for its state:
  the builds, which one is live, and what is left to do in Steamworks (the
  Linux depot in the developer and tester packages, the Linux runtime, and
  whether the Linux depot is for the Deck only).
- **The Android package name:** `stellanova.game` is a placeholder
  (`scripts/android.ps1`); Google Play keeps the first one uploaded
  forever. Which should it be?
- **Steam Input on Windows, for pith's apps (app 480):** when an app
  connects to Steam as Spacewar, Steam Input takes the Xbox pad from XInput
  (`hello_input`: "gamepad 1 disconnected" the moment Steam connects), since
  Spacewar's controls are Steam Input actions. Options: pith reads
  pads through Steam Input's API (an action manifest and a configuration per
  controller type, with the planned input actions, pith ADR 0027), or
  Steam Input turned off for Spacewar in Steam, or `--platform none` for
  input tests (what `hello_input` now suggests). The game connects as
  1096260 since its license works (2026-10-02), so its own default in
  Steamworks decides it there.
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
  more in dark gradients. Worth doing, and which first? Neither is
  measured yet; the AGM phone and the Pixel can measure them now.
- **PIX's analysis on the dev PC:** `pixtool save-event-list` fails with
  `E_PIX_ENABLE_EXPERIMENTAL_FEATURES_FAILED` under PIX 2402.07, though
  Developer Mode is on, so D3D12 captures open only in PIX's window. A
  newer PIX should fix it. RenderDoc's path (`pith/scripts/gpu-capture.ps1`)
  works for Vulkan and D3D12.
- **The effects' data on the web** (`docs/profiling.md`): copying the
  frame's effect vertices to the GPU is the web's largest cost that grows
  with the fight (0.28 ms a frame in wave 1). Expanding particles in the
  vertex shader would cut it about 7x. Now, or with the scale test (M1.8)?
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
