# 0012. Own-ship prediction, protocol 4 and a harder server

- Status: Proposed
- Date: 2026-10-03

## Context

- The developer as a tester (2026-10-03): the game works, but online the
  controls feel jerky and late; the ship should move at once, and be
  checked against the server's results and eased into place. Prediction
  should come now: later it would cost more.
- ADR 0003 (accepted 2026-10-03): the client predicts only the ship it
  flies directly; the server has authority.
- Until now the client sent its newest controls every 12 ms; the server
  applied the newest at each tick; every ship, ours too, was drawn 100 ms
  behind the newest snapshot. From the dev PC (91 ms round trip) a key
  moved the ship about 0.2 s later.
- The review of 2026-10-03 (`docs/reviews/2026-10-03.md`) found, on the
  public server: one Input with a NaN control poisoned every ship in the
  match; a repeated Hello left a ghost player, so the match never started
  over; a player who joined once 32 ships existed was in no snapshot and
  saw "Connecting" forever; peers that never said Hello held their slots;
  names could be taken or disguised; `/waves on` outlived the match.

## Decision

- **Protocol 4** (`sim/protocol.h`):
  - **Controls travel as 1/127 steps** (two signed bytes and the trigger):
    the server applies exactly what the client predicted, and no NaN or
    out-of-range value can come from the wire. Every float any message
    carries must be finite, or the message is refused.
  - **Input:** the client's ticks, at the server's rate (30 a second, on the
    frame's time), numbered from 1. Each message repeats the newest eight
    the server has not applied, so a lost one costs nothing.
  - **Snapshot:** one for each player: its own ship first, then the nearest
    ships and shots, up to 32 ships and 1200 bytes; the number of its last
    controls applied, and its gun's cooldown. A predicting client's own
    shots stay out: it draws them itself.
  - **Welcome:** the ship's hull and weapon, for the prediction, and whether
    the server flies it (autopilot: then nothing is predicted).
  - **Refusal:** a server turns a client away with a reason, another
    protocol version or a full match, instead of silence; the menu says
    which.
- **The server** (`server/src/server.cpp`):
  - It keeps each player's controls in order and applies one set a tick.
    When none has come, the last set holds, for half a second at most (a
    page in the background, a stalled link): then the ship lets go of the
    stick. More than three waiting (a burst after a stall) and the oldest
    go.
  - A peer must say Hello within 10 s; a second Hello is ignored; at most
    16 players (`MatchDesc::maxPlayers`).
  - Names are unique in a match: a taken one gets a number after it (Latin
    letters compared without case). `CleanText` drops invisible and
    direction-changing characters and makes every kind of space one plain
    space. A new name after joining is told to everyone and costs a chat
    line.
  - A match that starts over goes back to the waves it was started with.
- **The client** (`client/src/prediction.h`, `flight.cpp`):
  - Each of its ticks flies its own ship at once with the sim's own code:
    `sim::FlyShip`, `FireGun` and `BounceOffRocks`, the functions
    `sim::Step` calls. Its gun's shots show at once.
  - On each snapshot, the ship starts again from the server's word, and the
    controls the server has not applied fly again. What is drawn stays put
    and the jump fades out in about 0.1 s; a jump over 4 m (back home) shows
    at once.
  - Other ships are drawn as before, 100 ms behind the newest snapshot.
    Bumps with other ships are not predicted: the server's word corrects
    them.
  - Its own shots stop at the first rock or enemy on their way as drawn;
    what they hit is the server's to tell (events).
  - `--lag MS` and `--loss PERCENT` make any game's network worse: each
    message comes half the round trip late, and that share of snapshots and
    inputs is lost.

## Consequences

- Tested (`sn_tests`):
  - Over loopback with each way 3 or 5 ticks late, each of 250 and more
    predicted positions equals the server's after the same controls, a
    bounce off a rock and the gun's ticks included.
  - A ship flown alone matches the same ship stepped in a world, bit for
    bit.
  - Each server rule above: a second Hello, refusals for another version
    and a full match, a silent peer let go after 10 s, one set of controls
    a tick and the oldest dropped past three, controls let go when they stop
    coming, each player's ship first in
    its snapshot, unique names and the rename notice, the waves after a
    restart, non-finite floats refused.
- Inputs: 30 messages a second of up to 30 bytes, against 70 of 18 before.
  The server writes a snapshot for each player each tick.
- The Steam builds speak protocol 3: after the server is updated, they
  cannot join online games until the developer orders an upload
  (`docs/steam.md`).
- **Not yet:**
  - Clocks: when the client's ticks run a little faster or slower than the
    server's, the queue grows past three or runs dry now and then, and the
    ship takes a small correction. Pacing the client by the queue comes if
    it shows.
  - Lag compensation (ADR 0003): we aim at enemies drawn 100 ms in the past,
    plus the network's delay.
  - Predicted bumps with other ships; more than the nearest 32 ships;
    deltas; shots as events.
