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
17. Done: textures and content packs (pith ADR 0020). RGBA8 textures and
    samplers on every backend; a group of textures in one block of GPU
    memory (one VkDeviceMemory, one ID3D12Heap); textured sprites.
    `initial.pak` from `pith pak build` (a JSON manifest; test images made
    by code), read in the background during startup on every OS (next to
    the executable, UWP's package, APK assets stored uncompressed, the
    web's preload). Host image copy measured on the RTX 3080 (all paths
    under 1 ms for 1.6 MB; NVIDIA's tiling equals linear): uploads stay on
    staging until the phones and the Deck are measured
    (`docs/devices/uploads.md`).
18. Done: Dear ImGui drawn through pith's RHI (pith ADR 0021): one renderer
    for every backend with Android pre-rotation, colors correct on every
    backend, ImGui's own renderers dropped. The RHI gained vertex streams.
    Open: pre-rotation checked on the Pixel.
19. Done: materials v1 (pith ADR 0016, Accepted). A material type is a
    Slang file's group 1; shader blobs (version 3) record every block's
    fields, so parameters are set by name or through C++ structs that
    `ph_add_shaders` generates (`<file>.types.h`, offsets checked by
    `static_assert`). Parameters live in one persistent buffer, a 256-byte
    slot each, uploaded when they change.
20. Done: 3D scene and camera (pith ADR 0022). glTF conventions, reversed-Z
    Depth32Float depth on every backend, meshes with the position stream
    apart from the rest, a lit material (sun and ambient light in the frame
    block, now 192 bytes), orbit and free (WASD) cameras with ImGui
    controls. `hello_scene` is a screenshot test; Vulkan, D3D12 and Dawn
    differ by at most 1.
21. Done: generated primitives (cube, plane, UV sphere, geosphere, cylinder,
    cone), colored by normal or textured, all wound from their normals.
    Open: the geosphere's UV seam.
22. Done on Win32 (pith ADR 0017, "Implementation"): frame pacing v1.
    Present modes (FIFO, FIFO latest ready, mailbox, immediate) with
    fallbacks, a latency of 1-3 presented frames, waited for after each
    present (present wait 2 or 1 on Vulkan, the DXGI waitable object),
    statistics in the diagnostics window (latency from frame start to
    display, DXGI's refresh counts), Android's frame rate request. RTX 3080
    at 280 Hz: Vulkan latency is exactly 1, 2 or 3 refreshes; D3D12 one
    refresh more. Open: the phones, the Quest and the Deck; the driver holds
    mailbox and immediate to 280 Hz (pith's `docs/devices/pacing.md`).
23. Done on Win32 (pith ADR 0017, "Implementation"): HDR v1. Render
    targets; an RGBA16F scene target and the output pass (a hue-preserving
    shoulder to the display's headroom, UI at paper white, SDR, scRGB or
    HDR10 encoding); display color from Windows (HDR on, SDR white, peak);
    swapchains made again when HDR changes; the user's HDR setting and paper
    white in the diagnostics window. On the dev PC (HDR on, SDR white 240
    nits, peak 456): scRGB and HDR10 on Vulkan and D3D12, checked by
    screenshots turned back to SDR. Open: Android, the Deck, the web, UWP.
24. Present timing targets (Pixel now; the Deck once gamescope passes present
    timing through) and VRR. Deferred on 2026-10-01: only the Pixel and the
    Deck have `VK_EXT_present_timing`, and neither was reachable; done once
    the Pixel is back (see `questions.md`).
25. Done on Win32 (pith ADR 0023): devlink v1. Debug and Dev builds connect
    out to pith (TCP 7707, JSON lines; `adb reverse` for phones);
    `pith app list | info | logs | vars | set | screenshot | quit`, also as
    MCP tools. hello_scene's camera, sun and paper white are variables.
    Open: shader hot reload, the web (WebSocket), UWP's loopback exemption,
    a run on the phones and the Deck.
26. Done (pith ADR 0024): the editor backbone, `pith_editor_vulkan` and
    `_d3d12`. An ImGui docking app with a scene view (render targets of any
    size, shown in ImGui), a Slang shader editor that compiles 0.4 s after
    typing stops and swaps the lit pipeline between frames (about 0.2 s;
    errors show with line and column, the last good pipeline stays), the
    shader's material parameters by name, the log, and the diagnostics.
    Changes made to the file by other editors load too. Open: saved
    layouts, a scene format and selection (M0.6), undo.

The developer's list of 2026-10-01, after a round on the Pixel, the Galaxy
XR and the Steam Deck (the target: a game without spikes).

27. Done: GPU diagnostics and the device round (pith ADRs 0005, 0017). A
    latency of 1 presented frame by default, without the slider; ACES (Hill's
    fit) with 18% grey kept at every headroom; HDR found on the Deck through
    gamescope (HDR10, peak 1015 nits from the EDID). Per-pass GPU
    timestamps and pipeline statistics, debug labels for RenderDoc and PIX,
    the GPU memory budget, and the machine's memory and processor use, in
    the diagnostics window and `pith app stats`. All formats captured on
    every device (`docs/devices/README.md`); the Galaxy XR (Adreno 740)
    joined. Fixed: Android's exit crash (strict JNI checks) and devlink on
    Android (the INTERNET permission). Open: the Galaxy XR's and the
    Quest's capture while worn.
28. Done (pith ADR 0025): content sources and the sky. `content/sources.json`
    pins downloads (URL, SHA-256, license, credit) that CMake fetches into
    the shared cache; `content/local/` holds a developer's own files, never
    committed; manifests say `source:NAME` or `local:PATH`. Each app ships
    its own `<name>.pak` (they had overwritten each other's `initial.pak`).
    Cube maps from equirectangular OpenEXR or `.hdr` panoramas, stored as
    RGB9E5; `pith image cubemap` writes the faces. `render::DrawSky` draws
    one at the far plane; `hello_scene` shows NASA's Deep Star Maps.
    Verified on the RTX 3080 (three backends), the Deck and the Pixel.
    Open: BC6H and ASTC HDR, mipmaps, the developer's own panorama.
