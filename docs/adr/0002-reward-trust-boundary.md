# 0002. Only our dedicated servers grant persistent rewards

- Status: Accepted (2026-10-03, by the developer)
- Date: 2026-09-30

## Context

The game is F2P and sells chest slots that keep blueprints and character
DNA, so these items have real money value. Offline games (tutorial,
practice) run the server inside the client over loopback, and a player's
device can be tampered with: memory editors, modified builds, fake traffic.

## Decision

- Only dedicated servers that we run can report match results that grant
  persistent rewards (account progress, blueprints, chest contents).
- They report to the third-party backend with a server credential; clients
  can read their inventory but can never write rewards themselves (enforced
  by backend rules).
- Local loopback matches grant no persistent rewards; at most local-only
  flags such as "tutorial finished".

## Consequences

- Solo PvE with rewards is played online on a dedicated server. This matches
  the design goal that solo farming is inefficient; offline stays a practice
  and tutorial mode.
- Solo matches cost server time. They are small, so one server process can
  host many of them.
- The same simulation code still runs offline and online; only the reward
  report differs.
