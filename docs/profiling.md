# Profiling the game

How to measure Stella Nova with pith's own tools, and what they showed.
Update the numbers when they change.

## How

- **CPU and GPU together:** a Dev build, without validation layers (they
  cost 4x the frame's CPU time), on autopilot through the first waves:

  ```
  build/windows-msvc/bin/Dev/stellanova.exe --flight --autopilot --mute --platform none `
      --no-validation --capture prof.json --frames 6000
  ```

  - `prof.json` opens in https://ui.perfetto.dev. The CPU's scopes and,
    on a "GPU" track, each frame's passes and GPU labels are on one
    timeline (pith ADR 0005's addendum).
  - `prof.json.csv` sums up each scope: count, total, mean and max.
- **In Chrome:** the same arguments through `web-run.ps1`, which brings
  the CSV back:

  ```
  ..\pith\scripts\web-run.ps1 stellanova -BuildDir build\web -Arguments `
      '--flight --autopilot --mute --capture prof.json --frames 6000' `
      -PullFile prof.json.csv -PullTo prof-web.csv
  ```
- **One frame, draw by draw:** RenderDoc replays a frame and times every
  draw. `..\pith\scripts\gpu-capture.ps1` prints the passes, labels and
  slowest draws, and writes a CSV of all of them (pith ADR 0035):

  ```
  ..\pith\scripts\gpu-capture.ps1 -Exe build\windows-msvc\bin\Dev\stellanova.exe `
      -Arguments '--flight --autopilot --mute' -Frame 2500
  ```
- **A running game:** `pith app stats` gives the frame time, memory, and
  the GPU time of each pass and label; `pith app gpu-capture` captures
  its next frame, when it was started with `--gpu-tool renderdoc`.
- **What is timed:**
  - CPU scopes: `Game.Tick`, `Server.Update` (`Server.Bots`, `Sim.Step`,
    `Server.Send`), `Flight.Receive`, `Flight.Draw` (`Flight.Ships`,
    `Rocks`, `Scenery`, `Shots`, `Effects`, `Hud`), and pith's own (`rhi.*`,
    `render.*`, `text.*`).
  - GPU labels: ships, rocks, scenery, shots and pieces, sky, effects, hud.

## 2026-10-02: the first waves, on autopilot

The dev PC (RTX 3080, 2560x1440, FIFO at the display's 280 Hz), 6000
frames: wave 1 (2 bots) and the start of wave 2. Times are means per frame;
the server ticks at 30 Hz.

| | Windows (Vulkan) | Chrome (WebGPU, wasm) |
| --- | --- | --- |
| Frame | 3.57 ms (vsync) | 3.62 ms (the browser's pacing) |
| CPU work in a frame | 0.42 ms | 0.85 ms |
| `Flight.Draw` | 0.19 ms | 0.37 ms |
| `Flight.Effects` | 0.08 ms | 0.13 ms |
| `Flight.Hud` (text layout every frame) | 0.07 ms | 0.13 ms |
| `Flight.Scenery`, `Ships`, `Rocks` | 0.014, 0.010, 0.006 ms | 0.037, 0.027, 0.017 ms |
| Writing the frame's transient data | (mapped memory) | 0.28 ms |
| `Server.Bots` per tick | 0.016 ms | 0.036 ms |
| `Sim.Step` per tick | 0.009 ms | 0.016 ms |
| GPU frame | 0.052 ms | (WebGPU has no timestamps yet) |
| GPU: sky / effects / scenery / rocks | 0.018 / 0.003 / 0.002 / 0.001 ms | |

- **Frame pacing on Windows:** p50 3.571 ms, p99 3.661, p99.9 4.04, max
  6.99 ms. The one long frame waited two refreshes for its present, with
  0.6 ms of work in it.
- **One frame in RenderDoc:** 66-71 draws and 0.375 ms of GPU time in its
  replay. The sky, a full-screen draw, is most of it (replay times run
  higher than live ones).
- **Validation layers** (on in Dev): `Flight.Draw` 0.82 ms with them,
  0.19 ms without. Always profile with `--no-validation`.

The Steam Deck OLED (RADV, 1280x800, FIFO at 90 Hz), the same run of
2700 frames, after the bots were made half as fast and as quick to fire
(2026-10-02). It was run with `..\pith\scripts\linux-run.ps1 stellanova
-BuildDir build\linux-x64 -Arguments '... --capture prof-deck.json'
-PullFile prof-deck.json.csv`:

| | Steam Deck |
| --- | --- |
| Frame | 11.16 ms (vsync); p50 11.11, p99 11.37, p99.9 22.3, max 39.1 ms |
| CPU work in a frame | 0.74 ms |
| `Flight.Draw` | 0.36 ms (effects 0.16, HUD 0.11, scenery 0.03, ships 0.03) |
| `Server.Update` | 0.026 ms a frame; `Server.Bots` 0.044 ms a tick |
| GPU frame | 0.14 ms (sky 0.07, ships 0.015, effects 0.011, rocks 0.009) |

The slowest frames waited for the display (gamescope), with under 1 ms of
work in them.

Found and fixed along the way:

- **Web profiles were empty:** pith's backends overflowed wasm's stack at
  shutdown (pith ADR 0005's addendum).
- **A first-use hitch:** the first wave's banner, laid out in bold the
  first time, took 7-10 ms on the web. `Flight::WarmHud` lays out every HUD
  text once when a flight starts (14 ms there, among the start's other
  work).
- **A synchronization bug in pith's uploads:** a buffer's zeros, then its
  values, in one frame without a barrier. Found by synchronization
  validation, fixed (pith ADR 0034).

## The network

pith counts every server's, client's and peer's traffic (`ph::net::
TrafficStats`, pith ADR 0036): packets and bytes each way, the app's
messages, resent fragments, the round trip, and what waits to go out. The
game adds its messages by type (`sim::MessageCounts`).

- **The server's stats:** `stellanova-server` logs a line a minute while
  anyone plays (`--stats SECONDS` sets how often; 0 turns it off): players,
  ticks a second, packets and KB a second each way, each message type's
  rate and mean size, peers, round trip, resends, queue.
- **Debug mode:** SIGUSR1 switches the line to every second, with one more
  per player; the next SIGUSR1 switches it back. On the developer's server
  (`docs/web-test-deploy.md`), while watching the journal:

  ```sh
  systemctl kill -s USR1 stellanova-server     # on; again: off
  journalctl -fu stellanova-server -o cat
  ```

- **The client's Network window:** in Debug and Dev builds, F1 opens it
  beside pith's diagnostics: the server, the round trip (UDP; not known in
  browsers), packets and KB a second each way with a minute's graph, each
  message type's rate and mean size, resends and queue, totals, how far
  behind the newest snapshot ships are drawn, and snapshots dropped as late
  or doubled.
- **Captures:** `--capture prof.json` records the network's rates (KB and
  packets a second each way, every quarter second) as counters beside the
  CPU's scopes. The client takes it among the shell's options; the server
  too, in Dev builds, with `--seconds N` to stop by itself (Windows has no
  SIGTERM to send it):

  ```powershell
  build/windows-msvc/bin/Dev/stellanova-server.exe --listen "udp:127.0.0.1:27016" `
      --stats 1 --capture srv.json --seconds 30
  build/windows-msvc/bin/Dev/stellanova.exe --server udp:127.0.0.1:27016 --capture cli.json
  ```

### 2026-10-02: one player against wave 1

On the developer's server, from the dev PC (91 ms ping, 101 ms measured
over UDP), per player:

| | Out of the server | Into the server |
| --- | --- | --- |
| Packets a second | 30 | 70 |
| Traffic | 2.2 KB/s alone, 4.3 KB/s with 2 bots | 2.5 KB/s |
| Messages | snapshots, 30/s: 57 B alone, 129 B with 2 bots; events now and then (38 B); one Welcome of 12.8 KB (800 rocks) | inputs, 70/s, 18 B |

Found and fixed along the way:

- **Inputs every frame:** the client sent its controls each frame, 280
  times a second at 280 Hz, while the server takes the newest each tick
  (30 a second). Now at most every 12 ms: 60 a second at 60 Hz, 70 at more.
- **An acknowledgment for every datagram:** pith's channel answered each
  datagram that came in with one of its own, so the server sent about 280
  a second for 30 snapshots. Now only reliable data asks for an
  acknowledgment of its own; the others ride on the next datagram out,
  with how long they waited, so round trips stay exact (pith ADR 0036;
  UDP's handshake version 2).

Before both, at 280 Hz: about 280 packets a second each way per player;
after: 30 out and 70 in.

## What to do next

- **The web's transient data:** effects build 6 vertices of 24 bytes per
  particle on the CPU, and the web copies the whole ring to the GPU each
  frame (0.28 ms in a small fight, more with bigger waves). Expand
  particles in the vertex shader from a 16-20 byte record instead, about
  7x less to copy. This is the largest cost that grows with the action.
- **Text layout every frame:** the HUD shapes its 6-7 texts every frame.
  Cache a layout until its text changes, about 0.05 ms native and 0.1 ms
  on the web.
- **Repeated binds:** `render::DrawMesh` binds the pipeline, material and
  buffers for every rock. Cheap on desktop without validation (0.01 ms for
  the scenery). Sort and skip repeated binds once the scale test (M1.8)
  draws hundreds.
- **The phones:** none was connected on 2026-10-02. The Android build
  takes the same options.
- **The network, at scale:** a snapshot carries every ship and up to 64
  shots (at most 1200 bytes, about 36 KB/s a player). Interest management
  and deltas (ADR 0003) come before big matches; the Welcome's 12.8 KB of
  rocks could go as a seed.
