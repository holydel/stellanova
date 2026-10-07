# Deploying web test pages, and the game's server

pith's web samples and the game can be published on the developer's server
for testing on other devices (phones, headsets, other browsers). The same
machine runs the game's online server ([below](#the-game-server)). These
pages are live; the apps are Dev builds with the ImGui diagnostics window
(the game's opens with F1):

| Page | What | Files in `/srv/pith-web/` |
| --- | --- | --- |
| <https://wos-observer.com/test_webgpu.html> | `hello_clear_webgpu` | `test_webgpu.html`, `.js`, `.wasm` |
| <https://wos-observer.com/test_scene.html> (since 2026-10-01) | `hello_scene_webgpu`, under the star-map sky | `test_scene.html`, `.js`, `.wasm`, `.data` |
| <https://wos-observer.com/stellanova.html> (the game until 2026-10-04, since then its site: ADR 0015) | Stella Nova's site: the game, its ships and modules from the catalog, the leaderboard, Play | `stellanova.html`, `stellanova-site.css`, `stellanova-site.js`, `stellanova-catalog.json` and `stellanova-strings-en.txt` (copies of `content/catalog.json` and `content/strings/en.txt`), `stellanova-data/leaderboard.json` (the game server's) |
| <https://wos-observer.com/stellanova-play.html> (since 2026-10-04) | Stella Nova, this repository's `stellanova` | `stellanova-play.html` (the web build's `stellanova.html`), `stellanova.js`, `.wasm`, `.data` |
| <https://wos-observer.com/stellanova-admin.html> (since 2026-10-04, ADR 0016) | Stella Nova's admin page: the server's status, accounts, grants, bans, deletes. Only WOS Observer's superusers get in ([below](#the-admin-page)) | `stellanova-admin.html`, `stellanova-admin.js` |

`scripts/site-deploy.ps1` publishes the site and the game's page together
(`-SiteOnly` for the site alone), backing up the live files first; the
site's source is `site/` (its `README.md` says how to preview it).

Anyone with a page's address can open it: there is no password, and the
game's page and files carry its codename. The site's main page links the
game: a small "Stella Nova" in its footer, beside the visitor counts (since
2026-10-01; the site's release record `deploy/repairs/20261001-stellanova-link.md`
in `E:\wos-observer`).

An app with a content pack has a fourth file, `<app>.data`: Emscripten's
preload of the pack (`hello_scene.pak`, 7.9 MB), which the `.js` fetches
by that name before `main`. The game's holds two packs since 2026-10-04,
`stellanova.pak` and the menu's splash `stellanova-art.pak`: 18 MB; since
2026-10-05 the art pack holds the generated models and item pictures too,
and the `.data` is about 290 MB (the developer: fine on a fast line for
now; block compression and simpler meshes in pith's packs would shrink it).

**Which build:** Dev shows the debug UI (Dear ImGui: GPU, OS, frame times;
about 1.1 MB of wasm, 470 KB compressed). Retail compiles the UI out and is
about 60 KB, but shows only the clear color.

## The server

- `ssh -i C:\Users\holyd\.ssh\wos_observer root@178.128.248.85` (host
  `wos-observer-prod`).
- **Production:** it runs the WOS Observer site (Django behind Caddy 2.11 on
  ports 80 and 443). Change only what is described here.
- **Files:** pages live in `/srv/pith-web/`, owned by `root:caddy`, with
  mode 750 on the folder and 640 on files.
- **Caddy:** `/etc/caddy/Caddyfile`, in the `wos-observer.com` site. The
  `@pith_webgpu` matcher lists the exact paths that Caddy serves from
  `/srv/pith-web/`. Every other path still goes to the app. These files are
  served with `Cache-Control: no-cache`, so a new upload shows on the next
  reload.
- **The site's own releases replace that file:** WOS Observer's full release
  (`deploy/update-release.sh`) installs the release's `deploy/Caddyfile` as
  `/etc/caddy/Caddyfile`. Since 2026-10-01 the site's source
  (`E:\wos-observer\deploy\Caddyfile`) carries the same `@pith_webgpu` block,
  so its releases keep these pages. A page added here must be added there too.
- **The site's code** is changed only by its isolated patch releases (its
  `CLAUDE.md`): a staged copy, its tests, a database backup, an atomic switch.
- WebGPU needs HTTPS, which Caddy provides for the domain.

## Update the existing page

Build the web configuration (Dev for the debug UI, Retail without), then
upload the files. The page is renamed; the `.js`, `.wasm` and `.data` keep
their names, because the page and the script refer to them. For
hello_scene, upload `hello_scene_webgpu.html` as `test_scene.html` and add
`hello_scene_webgpu.data` to the second `scp`.

```powershell
D:\Projects\pith\scripts\build.ps1 web -Config Dev
$key = 'C:\Users\holyd\.ssh\wos_observer'
$server = 'root@178.128.248.85'
$bin = 'D:\Projects\pith\build\web\bin\Dev'
scp -i $key "$bin\hello_clear_webgpu.html" "${server}:/srv/pith-web/test_webgpu.html"
scp -i $key "$bin\hello_clear_webgpu.js" "$bin\hello_clear_webgpu.wasm" "${server}:/srv/pith-web/"
ssh -i $key $server 'chown root:caddy /srv/pith-web/*; chmod 640 /srv/pith-web/*'
```

The game's page keeps its name. The live files are copied to
`/root/backups/stellanova-web-<UTC time>/` first (since 2026-10-04), so
going back needs no build. The four new files go up under temporary names
and are moved into place together, so that nobody loads a new page with an
old `.wasm`:

```powershell
D:\Projects\stellanova\scripts\build.ps1 web -Config Dev -Target stellanova
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
ssh -i $key $server "mkdir -p /root/backups/stellanova-web-$stamp && cp -p /srv/pith-web/stellanova.* /root/backups/stellanova-web-$stamp/"
$bin = 'D:\Projects\stellanova\build\web\bin\Dev'
$names = 'stellanova.html', 'stellanova.js', 'stellanova.wasm', 'stellanova.data'
foreach ($name in $names) { scp -i $key "$bin\$name" "${server}:/srv/pith-web/.upload-$name" }
$moves = ($names | ForEach-Object { "chown root:caddy /srv/pith-web/.upload-$_ && chmod 640 /srv/pith-web/.upload-$_ && mv /srv/pith-web/.upload-$_ /srv/pith-web/$_" }) -join ' && '
ssh -i $key $server $moves
```

Check it (from the server itself, or any machine):

```powershell
ssh -i $key $server 'for p in /test_webgpu.html /hello_clear_webgpu.js /hello_clear_webgpu.wasm; do curl -s -o /dev/null -w "$p %{http_code} %{content_type}\n" https://wos-observer.com$p; done'
```

Expect 200, with `application/wasm` for the `.wasm`. A `.data` file comes
without a content type, which is fine: the script reads it as bytes. Caddy
compresses only text, JavaScript and wasm by default; the pack's RGB9E5 sky
would shrink by only a quarter, so it goes uncompressed.

## Add another page

1. Upload its files to `/srv/pith-web/` as above, for example
   `hello_window.html`, `.js` and `.wasm`.
2. Add their paths to the matcher, live and in the site's source
   (`E:\wos-observer\deploy\Caddyfile`, above). The Caddyfile has many dated backups
   (`Caddyfile.before-*`); keep that habit, and validate before reloading:

   ```sh
   cd /etc/caddy
   cp -p Caddyfile Caddyfile.before-pith-web-$(date -u +%Y%m%dT%H%M%SZ)
   cp -p Caddyfile Caddyfile.new
   # edit Caddyfile.new: append the new paths to the @pith_webgpu line
   caddy validate --config Caddyfile.new --adapter caddyfile
   mv Caddyfile.new Caddyfile && systemctl reload caddy
   ```

3. Check the new paths, and that `https://wos-observer.com/` still answers
   200.

If many pages are coming, one `handle_path /pith/*` block serving
`/srv/pith-web/` would replace the list of paths: then pages go to
`https://wos-observer.com/pith/<name>.html` without Caddy edits.

## The game server

Since 2026-10-02, `stellanova-server` runs the online skirmish that the
menu's Online item joins (ADR 0010); since 2026-10-04 also the campaign's
hub, its battles and its accounts (ADR 0014), and since that evening
sign-ins and the admin page (ADR 0016): only accounts signed in with Steam
are saved and ranked; guests last while they are connected.

- **Binary:** `/opt/stellanova/stellanova-server`, the `linux-x64` Retail
  build, owned by root (mode 755). It needs glibc 2.34 and GCC 13's
  libstdc++ at most; the machine has glibc 2.39.
- **Service:** `/etc/systemd/system/stellanova-server.service`, enabled.
  - Hardened: a dynamic user, a read-only system, no capabilities.
  - It writes two places only. Its state folder `/var/lib/stellanova`
    (`StateDirectory`; really `/var/lib/private/stellanova`, mode 700) holds
    the accounts, `accounts/<id>.json` (`--data`). The leaderboard goes to
    `/srv/pith-web/stellanova-data/leaderboard.json` (`--leaderboard`), a
    folder `root:caddy` with mode 2770 that the service may write
    (`ReadWritePaths`, `SupplementaryGroups=caddy`, `UMask=0027`: files 640,
    which Caddy reads); `/etc/caddy` and `/var/lib/caddy` are hidden from
    it.
  - **Back up the accounts** before risky changes:
    `tar czf /root/backups/stellanova-accounts-$(date -u +%Y%m%dT%H%M%SZ).tgz -C /var/lib/private stellanova`.
  - **Sign-ins:** `/etc/stellanova/signin.json` (root only, mode 600, in a
    700 folder) reaches the service as a systemd credential
    (`LoadCredential`, read as `--signin %d/signin.json`). It holds the
    providers' ids and secrets (`server/include/sn/server/providers.h`);
    today only Steam's entry, without its key until the developer adds it
    (below). The server calls Steam's Web API over HTTPS with the machine's
    libcurl and libcrypto.
  - **The admin listener:** `--admin ws:127.0.0.1:27081`, reached only
    through Caddy's gate (below).
  - Capped: 256 MB of memory, half a CPU, 32 tasks.
  - Restarted on failure.
  - The unit itself is the source: `systemctl cat stellanova-server`.
- **Ports:**
  - UDP 27015 on every address, for native clients. ufw allows it, as
    "Stella Nova game server".
  - WebSocket on 127.0.0.1:27080, for browsers, reached only through Caddy.
- **Caddy:** in the `wos-observer.com` site, before the application's
  catch-all, live and in the site's `deploy/Caddyfile`:

  ```text
  handle /stellanova/ws {
      reverse_proxy 127.0.0.1:27080
  }
  ```

  The backup from before it: `/etc/caddy/Caddyfile.before-stellanova-ws-20261001T212045Z`.
- **The site's `deploy/README.md`** names the service and the port, and
  points here.

### Steam's key

Steam sign-in needs the publisher Web API key of the Steamworks account
(partner.steamgames.com: Users & Permissions, Manage Groups, the group with
app 1096260, Create WebAPI Key). It is a secret: never in the repo. On the
server:

```sh
nano /etc/stellanova/signin.json      # "steam": {"key": "<the key>", "appId": 1096260, "identity": "stellanova"}
systemctl restart stellanova-server
journalctl -u stellanova-server -n 8 -o cat   # "sign-ins: steam"
```

Then the game, run from Steam or beside a running Steam client, says
"Signed in with Steam: saved" in the hub, and the log "account ... signs in
with steam". A refused ticket logs Steam's HTTP status and error.

### The admin page

<https://wos-observer.com/stellanova-admin.html>. Log in to WOS Observer's
admin first (<https://wos-observer.com/admin/>, as a superuser), then open
the page; without that login, the page and its connection redirect to the
login (which then lands on Django's groups page: open the admin page
again). Caddy's block, live and in the site's `deploy/Caddyfile`, before
the game server's:

```text
@stellanova_admin path /stellanova-admin.html /stellanova-admin.js /stellanova/admin/ws
handle @stellanova_admin {
    forward_auth 127.0.0.1:8000 {
        uri /admin/auth/group/
    }
    @stellanova_admin_ws path /stellanova/admin/ws
    handle @stellanova_admin_ws {
        @stellanova_admin_foreign not header Origin https://wos-observer.com
        respond @stellanova_admin_foreign 403
        reverse_proxy 127.0.0.1:27081
    }
    handle {
        root * /srv/pith-web
        header Cache-Control "no-cache"
        file_server
    }
}
```

Django's `/admin/auth/group/` answers 200 only to superusers (WOS
Observer's staff, its community admins, get 403). Every admin action goes
to the server's log (`journalctl -u stellanova-server | grep admin:`). A
deleted account's file moves to `deleted/` in the accounts folder.
- **The log:** `journalctl -u stellanova-server`, one line per client that
  connects, joins and leaves, and a stats line a minute while anyone plays.
- **Debug mode:** `systemctl kill -s USR1 stellanova-server` turns the
  stats to every second, with a line per player, without a restart; the
  same again turns it off. Watch with `journalctl -fu stellanova-server -o
  cat` (`docs/profiling.md` says what the lines hold).

### Update the server

Build it, upload it beside the old one, swap, restart. A restart ends the
match for whoever is in it.

```powershell
D:\Projects\stellanova\scripts\build.ps1 linux-x64 -Config Retail -Target stellanova-server
$key = 'C:\Users\holyd\.ssh\wos_observer'
$server = 'root@178.128.248.85'
scp -i $key D:\Projects\stellanova\build\linux-x64\bin\Retail\stellanova-server "${server}:/opt/stellanova/stellanova-server.new"
ssh -i $key $server 'cd /opt/stellanova && chown root:root stellanova-server.new && chmod 755 stellanova-server.new && mv stellanova-server.new stellanova-server && systemctl restart stellanova-server && systemctl is-active stellanova-server'
```

When the protocol changes (`sim::PROTOCOL_VERSION`), update the web page
too: a client of another version is turned away.

### Check it

```powershell
ssh -i $key $server 'systemctl status stellanova-server --no-pager; journalctl -u stellanova-server -n 20 --no-pager'
D:\Projects\stellanova\scripts\run.ps1 -Arguments '--online --frames 600'
D:\Projects\stellanova\scripts\run.ps1 -On web -Arguments '--online --frames 600'
```

The server's log shows each run: "connected over UDP" (or "a WebSocket
client connected"), "player N joined", "left", and "the match starts
over". From the server itself, Caddy's route answers an upgrade with 101:

```sh
curl -s -o /dev/null -w "%{http_code}\n" --http1.1 -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" --max-time 3 https://wos-observer.com/stellanova/ws
```

## Undo

Each backup is the Caddyfile from before one change: restoring an older one
also removes the changes made after it.

- The sign-ins and the admin page of 2026-10-04 evening (ADR 0016): the
  old binary and unit are in
  `/root/backups/stellanova-server-20261004T174544Z/` (protocol 5, guests
  saved), the accounts as they were in
  `/root/backups/stellanova-accounts-20261004T174544Z.tgz`, the web files in
  `/root/backups/stellanova-web-20261004T174657Z/`. Copy the binary and the
  unit back, `systemctl daemon-reload`, restart; copy the web files back
  (`cp -p`) and delete `stellanova-admin.*`; restore
  `/etc/caddy/Caddyfile.before-stellanova-admin-20261004T174544Z`
  (validate, reload) and revert the site's `deploy/Caddyfile` (record
  `deploy/repairs/20261004-stellanova-admin.md`). `/etc/stellanova` can
  stay or go.
- The game server only:

  ```sh
  systemctl disable --now stellanova-server
  rm /etc/systemd/system/stellanova-server.service && systemctl daemon-reload
  rm -r /opt/stellanova
  ufw delete allow 27015/udp
  ```

  Then remove the `/stellanova/ws` block from `/etc/caddy/Caddyfile`
  (validate, `systemctl reload caddy`), and from the site's
  `deploy/Caddyfile`, with the line about it in its `deploy/README.md`.
  Restoring `Caddyfile.before-stellanova-ws-20261001T212045Z` removes the
  block too. The game's Online item then says the server did not answer.
- The game's page, back to the version before the last upload: copy the
  files of the newest `/root/backups/stellanova-web-*/` into
  `/srv/pith-web/` with `cp -p`, which keeps `root:caddy` and mode 640.
- The site of 2026-10-04 (ADR 0015), back to the game at
  `/stellanova.html`: copy the files of
  `/root/backups/stellanova-web-20261004T161601Z/` (the game as it was:
  protocol 4) into `/srv/pith-web/`, delete `stellanova-play.html`,
  `stellanova-site.*`, `stellanova-catalog.json`,
  `stellanova-strings-en.txt` and `stellanova-splash.jpg`, and restore
  `/etc/caddy/Caddyfile.before-stellanova-site-20261004T161444Z` (validate,
  reload; the site's record `deploy/repairs/20261004-stellanova-site.md`).
  The server of that day: its old binary and unit are in
  `/root/backups/stellanova-server-20261004T161424Z/` (protocol 4, no
  accounts); the accounts stay in `/var/lib/private/stellanova`, the
  leaderboard's folder in `/srv/pith-web/stellanova-data`.
- The game's page only: remove the footer link first (the site's release
  record has the undo), then restore
  `/etc/caddy/Caddyfile.before-stellanova-20261001T152552Z`, run
  `systemctl reload caddy`, and delete `stellanova.*` from
  `/srv/pith-web/`.
- The scene page only: restore
  `/etc/caddy/Caddyfile.before-pith-scene-20261001T043830Z`, run
  `systemctl reload caddy`, and delete `test_scene.html` and
  `hello_scene_webgpu.*` from `/srv/pith-web/`.
- Everything: undo the game server as above, then restore
  `/etc/caddy/Caddyfile.before-pith-webgpu-20260930T114040Z`, run
  `systemctl reload caddy`, and delete `/srv/pith-web/`.

## Notes

- Pages log to the browser console; warnings, errors and a failed exit also
  show on the page itself.
- A gamepad that switches itself off as soon as a page reads it (the light
  goes out, on any gamepad page): restart Chrome. Its gamepad service had
  got into that state on the dev PC on 2026-10-01; native apps were fine. Dev pages contain emrun's log relay, which stays
  off unless the URL names `localhost`.
- Nothing about this server belongs in the pith repository, which will be
  public.
