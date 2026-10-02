# 0010. The online server: stellanova-server on the developer's machine

- Status: Proposed
- Date: 2026-10-02

## Context

- The developer (2026-10-01, late evening): "next - the real server",
  hosted on their server (the wos-observer.com machine), with clients
  connecting to it.
- ADR 0007: local games run the server in the client over loopback;
  `stellanova-server` was to come with pith's UDP transport.
- ADR 0003: the server sends snapshots; the client draws between them,
  predicts its own ship and gets lag compensation later.
- pith ADR 0036: UDP for native clients, WebSocket for browsers (which have
  no UDP), one server listening on both.
- The machine: a 2-CPU, 4 GB Ubuntu 24.04 droplet that runs the WOS
  Observer site behind Caddy and already serves the game's web page
  (`docs/web-test-deploy.md`). The site must not suffer.

## Decision

- **`stellanova-server`** (`server/src/main.cpp`) runs the match's `Server`
  alone, on Linux (and Windows, for tests).
  - One co-op skirmish against the bots' waves, which everyone who connects
    joins. When the last player leaves, the match starts over
    (`MatchDesc::resetWhenEmpty`); an empty match does not tick.
  - It listens at `udp:0.0.0.0:27015` and `ws:127.0.0.1:27080` by default
    (`--listen "<addresses>"`; `--no-bots`).
  - It updates every millisecond while someone plays, every 10 ms
    otherwise. It logs to its output and stops on SIGTERM.
  - Stats: a line a minute while anyone plays (`--stats`), every second
    with a line per player in debug mode (SIGUSR1 toggles it), and in Dev
    builds `--capture` (`docs/profiling.md`).
- **Where:** the wos-observer.com machine, as `stellanova-server.service`.
  - systemd gives it a user of its own and a read-only system, without
    capabilities, and caps it at 256 MB and half a CPU.
  - UDP 27015 is open in the firewall.
  - Caddy serves the WebSocket at `wss://wos-observer.com/stellanova/ws`
    (its TLS, proxied to 127.0.0.1:27080).
  - The unit, updates and undoing it: `docs/web-test-deploy.md`.
- **The client:** the main menu's Online item (and `--online`) flies on
  that server.
  - Native builds connect over UDP (`udp:wos-observer.com:27015`); the web
    build over `wss://wos-observer.com/stellanova/ws`. `--server ADDRESS`
    names another server.
  - The flight screen says "Connecting to" and the server's name until the Welcome. When
    the server never answers, or goes away, the menu comes back and says
    which.
- **Inputs** go at most every 12 ms (60 a second at 60 Hz): the server
  takes the newest each tick.
- **Drawing what arrives unevenly:** the client keeps the last 8 snapshots
  and draws at a render tick that runs with the frame's time.
  - That tick stays 3 ticks (100 ms) behind the newest snapshot online, 1
    in a local game, pulled back gently as snapshots come; 4 ticks off, it
    jumps.
  - Ships are drawn between the two snapshots around it. Shots move on
    from the snapshot they were in.
  - A late or doubled snapshot is dropped.
- **Trust:** no accounts and no encryption yet (pith ADR 0036). Anyone can
  join, and the server takes nothing from a client but its controls.

## Consequences

- Measured on 2026-10-02 from the dev PC (91 ms round trip):
  - A native client flies its ship 0.34 s after it starts connecting: the
    handshake, Hello, and a Welcome with 800 rocks.
  - A browser flies 0.43 s after it starts: TLS and the WebSocket upgrade
    come first.
  - Idle, the server uses 4 MB and about 0.9% of one CPU, mostly the
    kernel's work for its 100 wake-ups a second.
  - Per player against wave 1: 30 packets a second out (2-4 KB/s), 70 in
    (2.5 KB/s); a Welcome of 12.8 KB.
- Verified: Windows over UDP, and Chrome on the deployed page over wss,
  join, meet wave 1 and leave; the match starts over behind them.
- **Our own ship answers late online:** a round trip plus the drawing
  delay, about 0.2 s from the dev PC. Own-ship prediction (ADR 0003) is
  next.
- A server restart ends the match for everyone in it.
- The client and the server speak one protocol version
  (`sim::PROTOCOL_VERSION`): the web page and the server are updated
  together. A client of another version is turned away and reads "The
  server did not answer"; a clearer message needs a refusal in the
  protocol.
- **Not yet:**
  - Several matches, and a choice between them.
  - Interest management: a snapshot carries 32 ships at most.
  - Connect tokens, encryption and accounts (ADR 0005).
  - Rate limits beyond the transports' caps (64 peers each; per-peer
    queues).
  - Metrics beyond the log's stats lines.
