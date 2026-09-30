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

- CMake presets per OS; `core` (types, log, assert, time, arenas);
  doctest; a local `check` script.
- Done when: `check` builds and runs the tests on Windows.
- **Done 2026-09-30.** Windows presets only (MSVC, clang-cl, VS solution);
  other OSes get presets with the OS layer (M0.2).

### M0.2 OS layer, window and loop

- `ph::os` API in SDL3's shape (pith ADR 0008) with backends; `tick()`-driven
  loop; profiling from the first frame; startup time from process start to
  the first frame.
- Done when: an empty window runs on Win32 and UWP, and the headless tests
  pass on both. (Linux moved to "Later in Phase 0".)
- **Progress 2026-09-30:**
  - done: profiling barebone; the `ph::os` API; the `headless`, `win32` and
    `uwp` backends;
  - done: the `hello_window` sample, smoke-tested in `check` on Win32 and
    with `uwp-run.ps1` on UWP.
  - Done: the headless tests run on UWP. M0.2 is done for Win32 and UWP.

### M0.3 Clear color on every graphics backend

- `ph::rhi` v0 (device, swapchain, frame, clear, present) with a Null
  backend; Vulkan, D3D12 and WebGPU backends; web and Android OS backends.
- Done when: an animated clear color runs on Win32 (Vulkan, D3D12, WebGPU on
  Dawn), UWP (D3D12), Chrome (WebGPU), Pixel and Quest 3 in flat mode
  (Vulkan).

### M0.4 Shaders and the triangle

- Shader compiler (the Slang library; SPIR-V, DXIL, MSL and WGSL blobs) with
  a command-line tool that CMake runs at build time; RHI buffers and
  pipelines; offscreen screenshot and compare tool.
- Done when: the triangle screenshot test passes on every OS of M0.3.

### M0.5 Debug UI and editor backbone

- Dear ImGui drawn through our RHI (fps, OS, GPU) on every OS; the editor
  app (ImGui docking) on Win32; Slang in the editor (edit, recompile, see).
- Done when: the editor recompiles a shader and shows the result live.

### M0.6 Scene graph

- ADR for a state-of-the-art, data-oriented scene graph with hierarchy;
  implementation; a stress sample with many animated nodes.
- Done when: the sample runs on every OS, with a recorded benchmark.

### Later in Phase 0

- Linux and Steam Deck through the `sdl3` OS backend (from M0.2).
- Apple: Metal backend, macOS and iOS OS backends.
- Tracy behind the profile macros; startup time from process creation;
  parallel initialization (GPU device, window, audio) to fit the 2 s target.
## Phase 1: offline core

- **M1.1 Starfield and a ship.** Baked mesh format v0 (glTF in, runtime
  layout out); `sim/` with a fixed tick; loopback server; direct control
  (keyboard, mouse, gamepad, touch). Measure "2 seconds to starfield".
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

## Short-term plan

The developer's plan of 2026-09-30, reordered for dependencies. The platform
(store) stays `none` throughout.

1. Done: Win32 window.
2. Done: UWP window.
3. Done: headless backend tested on Win32 and on UWP (the tests run as a UWP
   console app inside the app container, through ctest).
4. Done: `ph::rhi` v0 API and the Null backend: device, swapchain, a frame
   with a clear color, present; tests.
5. Done: Vulkan backend: animated clear color on Win32 (`hello_clear_vulkan`);
   swapchain resize; the loop keeps ticking during live resize.
6. Done: D3D12 backend: animated clear color on Win32 and UWP. Retail first
   tick: 365 ms with D3D12 vs 555 ms with Vulkan (the Vulkan swapchain costs
   ~250 ms on NVIDIA; device creation ~300 ms on both).
7. Done: WebGPU backend on Dawn native: animated clear color on Win32
   (`hello_clear_webgpu`, Dawn on D3D12 with a pinned DXC). Retail first tick:
   ~900 ms, most of it Dawn's device creation.
   Then asynchronous startup (pith ADR 0010): the device is created in the
   background while the app ticks. Retail first tick: ~11 ms on every
   backend; the device is ready at ~310 ms (Vulkan), ~350 ms (D3D12) and
   ~910 ms (Dawn).
8. Done: web OS backend (Emscripten: canvas, keyboard, mouse, focus and
   resize; the browser drives the loop) and WebGPU in Chrome through
   emdawnwebgpu. Tests run in Node; `scripts/web-run.ps1` runs a page in
   Chrome and prints its log. From page load: first tick ~470 ms, device
   ready ~740 ms (Dev, local server).
9. Done on Quest 3 (flat mode) and Pixel 9 Pro XL: Android OS backend on
   GameActivity (pith ADR 0011: APKs from Gradle, tests over adb). Window,
   lifecycle (surface lost and restored), keys and pointer;
   `scripts/android-run.ps1` installs, starts and logs an app. Retail:
   launch 162 ms on Quest and 123 ms on Pixel (Android's measure), first
   tick about 10 ms, APK 1.2 MB with R8.
10. Done on Pixel 9 Pro XL (Quest 3 not tested yet): Android with Vulkan,
    animated clear color (`hello_clear_vulkan`). The swapchain survives Home,
    rotation and split screen. Retail: launch 110 ms, Vulkan device ready at
    8 ms, APK 1.6 MB. Open: Android holds games at 60 Hz unless they ask for
    a frame rate.
10b. Done on Steam Deck (OLED, SteamOS 3.7, Desktop Mode): Linux x64,
    cross-compiled on Windows against the Steam Runtime 4 SDK sysroot; own
    X11 backend (xcb); Vulkan clear at 90 Hz on RADV; tests and apps run over
    SSH. Fullscreen on every OS layer that has it (X11, Win32, web; Android
    and UWP later). Open: Game Mode (gamescope) run, gamepad input.
11. Done: Dear ImGui everywhere, in Debug and Dev builds (pith ADR 0012): a
    diagnostics window (GPU, OS, frame times, window and display,
    fullscreen) on Win32 (Vulkan, D3D12, WebGPU), UWP, web, Android (Pixel)
    and the Steam Deck (Game Mode). The web test page shows it. Upstream
    ImGui 1.92.9b-docking with ImGui's own renderers; pith's OS layer feeds
    its input. Open: safe-area insets on phones.
12. Done: shaders and tools for agents. The `pith` tool (pith ADR 0014):
    `pith shader compile` (Slang to SPIR-V, DXIL or WGSL, with the vertex
    inputs from reflection) and `pith embed`, with text or JSON output; `pith
    mcp` serves the same commands as MCP tools (`.mcp.json` in both repos).
    CMake compiles shaders at build time and embeds them in the executable
    (pith ADR 0013: embedded now; content packs with the first real asset).
    The design principles are in pith's `docs/design-principles.md`.
13. Done: the triangle on every OS (`hello_triangle`), from compiled shaders
    and pipelines. No buffers yet, at the developer's request: the vertices
    come from SV_VertexID. Verified on Win32 (Vulkan, D3D12, Dawn), UWP (log
    only), Chrome, Pixel 9 Pro XL, Quest 3 (flat) and the Steam Deck (Game
    Mode). Pipeline creation: 16 ms on Vulkan (RTX 3080), 5–14 ms on phones
    and Quest, 6 ms on the Deck.
13c. Done: what Vulkan reports on every test device, in pith's
    `docs/devices/` (the `vulkan_caps` app, `scripts/capture-vulkan-caps.ps1`),
    with a comparison table. Four of five GPUs are UMA with all video memory
    CPU-writable; the RTX 3080 has no resizable BAR (256 MB window); present
    timing on the Pixel and the Deck's driver; HDR10 and scRGB on the RTX
    3080, the Deck (Game Mode) and the Pixel.
14. Done: GPU data v1 (pith ADR 0015, Accepted): buffers with queue-ordered
    updates, the transient ring, bind groups from shader reflection (blob
    version 2), draw constants (128 bytes: push constants, root constants,
    a uniform fallback on WebGPU), index buffers, blend modes and culling.
    The frame block, group 0 of every shader (`shaders/pith.slangh`, the
    `render` module): view-projection with pre-rotation, viewport, time.
    `hello_triangle` spins three triangles from draw constants.

The developer's list of 2026-09-30 (evening), merged with steps 15-21 of
the design (pith ADRs 0015-0017). Answers that shape it: world units for 2D,
pre-rotation in shaders (ImGui included), batches of 65536 vertices rather
than 4-vertex instances, 1M small sprites on every device, one preloaded
`initial.pak`, RGBA8 without mipmaps first, generated test images, own
math, glTF conventions (right-handed, +Y up, meters, XZ ground) with
reversed-Z depth, a simple object list with orbit and free cameras, vertex
streams split into position and the rest.

15. Done: screenshots (pith ADR 0018): the swapchain read back on every
    backend, `--screenshot` in samples, `run.ps1 -Screenshot` from any
    target (UWP and the web checked by screenshot for the first time),
    `pith image compare`, screenshot tests per Win32 backend. Open: Android
    pre-rotation on the Pixel.
16. Done: the 2D canvas (pith ADR 0019): world units under a 2D camera,
    sprites fetched by vertex index in batches of 16384, shape and color
    streams, four blend modes, sprite buffers for sprites that stay.
    `hello_sprites --sweep` on the RTX 3080: 1.76M moving sprites at 60 Hz
    (the single-thread CPU update limits), 3M still sprites at 280 Hz;
    Chrome 1.68M moving. Open: the Quest, the Pixel and the Deck
    (`docs/devices/sprites.md`).
17. Textures: RGBA8 without mipmaps, samplers, textured sprites; one
    preloaded `initial.pak` whose images upload into one GPU memory block,
    sliced into textures; test images generated by code. Measured:
    staging copies versus host image copy, and pre-swizzled images cached
    per device and driver.
18. Dear ImGui drawn through pith's RHI, so it gets pre-rotation on Android
    and drops ImGui's three renderers.
19. Materials v1 (ADR 0016): types from Slang files, parameters in one
    persistent buffer, C++ structs from reflection.
20. 3D scene and camera: depth (reversed Z, 32-bit float), vertex streams
    (position apart from the rest), an object list, orbit and free (WASD)
    cameras with ImGui controls.
21. Generated primitives (cube, geosphere, cylinder, plane, UV sphere,
    cone), colored and textured, with one directional light and ambient.
22. Frame pacing v1: present modes, waiting for earlier presents
    (present_wait2, the DXGI waitable object, fences), timing statistics in
    the diagnostics window, Android's frame rate request.
23. HDR: an RGBA16F scene target and the output pass (tone map, UI at paper
    white, SDR/scRGB/HDR10 encoding), display color info from each OS,
    runtime HDR changes, the user's HDR on/off setting.
24. Present timing targets (Pixel now; the Deck once gamescope passes present
    timing through) and VRR.
25. Devlink: a small dev-only server in apps (logs, screenshots, variables,
    shader hot reload from the PC to every device), reached through
    `pith mcp`.
26. Editor backbone: an ImGui docking app on Win32, with Slang shaders edited
    and reloaded live.
