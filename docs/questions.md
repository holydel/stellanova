# Open questions and device checks

Collected during the overnight run of 2026-09-30 to 2026-10-01 (roadmap
steps 14 onwards). Each item says what is blocked and what an answer or a
device session would settle. Remove items once answered.

## Devices that were not reachable

- **Quest 3:** never visible to adb all night. Nothing from steps 14 on
  has run on it.
- **Pixel 9 Pro XL:** connected but locked, then disconnected. Android
  builds pass; nothing ran on the phone.
- **Steam Deck:** no SSH. Linux builds pass; tests were skipped.

## To check on devices

- **Pre-rotation on the Pixel** (steps 15, 16, 18): sprites, ImGui and
  scissors in portrait and both landscapes, by screenshot
  (`run.ps1 <sample> -On android -Screenshot`).
- **Sprite throughput** (step 16, `hello_sprites --sweep`) on the Quest, the
  Pixel and the Deck. The goal is 1M small sprites on every device
  (pith's `docs/devices/sprites.md`).
- **Host image copy** (step 17, `hello_textures --measure-uploads`) on the
  phones and the Deck. The results decide whether packs should hold
  textures pre-swizzled for the device (pith's `docs/devices/uploads.md`).
- **Frame pacing** (step 22) on the Pixel, the Quest and the Deck: which
  wait each uses, the latency at each setting, and whether the Pixel
  switches to 120 Hz on the frame rate request (pith's
  `docs/devices/pacing.md`).
- **Screenshot comparison on the web and on devices** needs fixed-size
  offscreen targets. Window sizes differ there, so today only Win32 has
  screenshot tests.

## Decisions for the developer

- **NVIDIA driver settings on the dev PC:** every present mode, mailbox and
  immediate included, runs at 280 Hz, even fullscreen. `vkQueuePresentKHR`
  blocks and DXGI counts one refresh per frame. Is "Vertical sync" forced
  on in the NVIDIA Control Panel (a common G-SYNC setup)? If so, can it be
  set to "Use the 3D application setting" for pith's executables, so the
  modes can be compared?
- **Android frame rate request strategy:** the samples ask for the
  display's highest rate with "only if seamless". "Always" would also
  switch modes that blank the screen for a moment. Which should a game use
  at startup?
- **Default latency:** 2 presented frames (7.1 ms at 280 Hz on Vulkan);
  1 halves it but leaves no slack for an uneven frame. Keep 2 as the
  default, with 1 as a player setting?

- **Canvas sprites in passes with depth:** sprite pipelines have no depth
  variant yet. Add one per depth format (as ImGui does), or keep 2D in
  passes of its own?
- **Geosphere UV seam** (step 21): vertices on the seam are not split yet,
  so a texture wraps wrongly along one meridian. Fix now, or when a
  textured planet needs it?
- **CPU sprite update:** single-threaded, it limits moving sprites to about
  1.76M at 60 Hz on the RTX 3080. A job system is the next step; is now
  the right time, or after the frame pacing and HDR steps?
