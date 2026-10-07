# The game's website

The page about the game at `https://wos-observer.com/stellanova.html`
(ADR 0015): what the game is, how it plays, the ships and modules with their
stats, the leaderboard, and a Play button. The game itself moves to
`stellanova-play.html`. Static files only: no framework, no outside fonts,
scripts or trackers.

## Files

| File | What it is | Where it comes from |
| --- | --- | --- |
| `stellanova.html` | The page, with the stat icons as an SVG sprite | here |
| `stellanova-site.css` | Its styles | here |
| `stellanova-site.js` | Reads the data below and builds the cards and the leaderboard | here |
| `stellanova-catalog.json` | Every number of the game | a copy of `content/catalog.json`, made at deploy time |
| `stellanova-strings-en.txt` | Names: hulls, modules, ammo, damage types, resources, slots, credits | a copy of `content/strings/en.txt`, made at deploy time |
| `stellanova-data/leaderboard.json` | The leaderboard | written by the game server; the one here is a sample |
| `stellanova-splash.jpg` | The hero's picture (optional) | not in the repository; see below |
| `stellanova-admin.html`, `stellanova-admin.js` | The admin page (ADR 0016): the game server's status, accounts and actions, over the server's admin connection | here |

The two copies and the splash are git-ignored: the deploy copies them from
`content/`, so the site and the game never disagree.

## How the page reads its data

- **The catalog:** hulls, modules, ammo, and the rules, colony, map and
  enemy numbers that the "How it plays" cards quote. The page holds no game
  numbers of its own. Derived numbers (acceleration, turn rate, damage a
  shot, DPS, and so on) are worked out in the script and marked "derived".
  The damage split follows the game's rule (`SplitDamage` in
  `sim/src/world.cpp`), and magazine shots are counted as
  `sim/src/fitting.cpp` counts them.
- **The strings:** names only (`hull.*`, `module.*`, `ammo.*`, `class.*`,
  `slot.*`, `damage.*`, `resource.*`, `credits.*`). Stat labels are the
  site's own, since the game's `stat.*` texts are written for the game's
  screens.
- **The leaderboard:** `{"season", "updated", "entries": [{"rank", "name",
  "ring", "at", "provider"}]}`, times in Unix seconds; only signed-in
  players are on it (ADR 0016). The page shows the top 10. Names
  are player text and go in as text, never as HTML. With no file, or no
  entries, the page says that no one has cleared a ring yet.
- If the catalog does not load, the page says so and hides the cards; the
  rest still shows.

## The leaderboard on the server

The game server writes it with `--leaderboard FILE` (ADR 0014;
`server/src/main.cpp`), into `/srv/pith-web/stellanova-data/`, a folder that
Caddy serves and the server may write to.

## The splash

Optional. The deploy can make it from the menu's splash,
`content/generated/splash/menu.png`, as a 1920 px JPEG (about 80 KB). Without
it, the page draws a planet in CSS, and the browser logs one 404 for the
missing picture.

## Deploy

Files go flat into `/srv/pith-web/` beside the game's (`docs/web-test-deploy.md`):
the page, its CSS and script, the two copies, the splash, and the
`stellanova-data/` folder. Caddy's `@pith_webgpu` matcher needs these paths
added, live and in the site's source. `scripts/site-deploy.ps1` (ADR 0015) is
not written yet.

## Preview locally

```powershell
cd D:\Projects\stellanova\site
Copy-Item ..\content\catalog.json stellanova-catalog.json
Copy-Item ..\content\strings\en.txt stellanova-strings-en.txt
python -m http.server 8000
```

Then open <http://localhost:8000/stellanova.html>. The Play button opens
`stellanova-play.html`, which exists only on the server. A page opened as a
file (`file://`) cannot fetch its data: use the server.

## The admin page

`stellanova-admin.html` speaks ADR 0016's admin connection: JSON over
WebSocket to `/stellanova/admin/ws`, which Caddy lets through only for WOS
Observer's superusers (`docs/web-test-deploy.md`). To try it on this
machine, run the server with `--admin ws:127.0.0.1:27081`, serve this
folder (`python -m http.server`), and open
`stellanova-admin.html?ws=ws://127.0.0.1:27081` (the override takes only
this machine's addresses). Player text goes in as text, never as HTML.
