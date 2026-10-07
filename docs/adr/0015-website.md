# 0015. The game's website: a page about the game, and the game apart

- Status: Proposed
- Date: 2026-10-04

## Context

- The developer (2026-10-04): show the blueprints with their stats and
  stat icons on wos-observer.com; split the game's page in two: a site
  about the game with a leaderboard preview and a Play button, and the
  game itself.
- Today `https://wos-observer.com/stellanova.html` is the game (the web
  build), linked from the site's footer (`docs/web-test-deploy.md`).

## Decision

- **`stellanova.html` becomes the site:** what the game is, the ships and
  modules with their stats, the leaderboard, and Play. One page in
  `site/` with its own CSS and script; no framework, no outside fonts or
  scripts.
- **The game moves to `stellanova-play.html`.** Its `.js`, `.wasm` and
  `.data` keep their names.
- **Stats come from the game's catalog:** the page reads
  `stellanova-catalog.json`, a copy of `content/catalog.json`, so the
  site and the game never disagree.
- **Stat icons** are small SVG drawings in the page (a line style, tinted
  by the page's colors). The game gets its own vector icons later (roadmap
  step 50).
- **The leaderboard** is the server's `leaderboard.json` (ADR 0014),
  served by Caddy from `/srv/pith-web/stellanova-data/`, a folder the game
  server may write to.
- `scripts/site-deploy.ps1` uploads the site and the game's page, backing
  up the live files first, as `docs/web-test-deploy.md` does by hand.

## Consequences

- Caddy's list of paths gains the new files, live and in the site's
  source (`E:\wos-observer\deploy\Caddyfile`).
- The footer's link now opens the site, which links the game.
- Blueprint pictures wait for the ships' design sheets (roadmap steps
  49-52); until then the cards show the stats and a drawn silhouette.
