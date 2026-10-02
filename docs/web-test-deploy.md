# Deploying web test pages, and the game's server

pith's web samples and the game can be published on the developer's server
for testing on other devices (phones, headsets, other browsers). The same
machine runs the game's online server ([below](#the-game-server)). Three
pages are live, all Dev builds with the ImGui diagnostics window (the
game's opens with F1):

| Page | App | Files in `/srv/pith-web/` |
| --- | --- | --- |
| <https://wos-observer.com/test_webgpu.html> | `hello_clear_webgpu` | `test_webgpu.html`, `.js`, `.wasm` |
| <https://wos-observer.com/test_scene.html> (since 2026-10-01) | `hello_scene_webgpu`, under the star-map sky | `test_scene.html`, `.js`, `.wasm`, `.data` |
| <https://wos-observer.com/stellanova.html> (since 2026-10-01) | Stella Nova, this repository's `stellanova` | `stellanova.html`, `.js`, `.wasm`, `.data` |

Anyone with a page's address can open it: there is no password, and the
game's page and files carry its codename. The site's main page links the
game: a small "Stella Nova" in its footer, beside the visitor counts (since
2026-10-01; the site's release record `deploy/repairs/20261001-stellanova-link.md`
in `E:\wos-observer`).

An app with a content pack has a fourth file, `<app>.data`: Emscripten's
preload of the pack (`hello_scene.pak`, 7.9 MB), which the `.js` fetches
by that name before `main`.

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

The game's page keeps its name, so all four files go up as they are:

```powershell
D:\Projects\stellanova\scripts\build.ps1 web -Config Dev -Target stellanova
$bin = 'D:\Projects\stellanova\build\web\bin\Dev'
scp -i $key "$bin\stellanova.html" "$bin\stellanova.js" "$bin\stellanova.wasm" "$bin\stellanova.data" "${server}:/srv/pith-web/"
ssh -i $key $server 'chown root:caddy /srv/pith-web/*; chmod 640 /srv/pith-web/*'
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
menu's Online item joins (ADR 0010).

- **Binary:** `/opt/stellanova/stellanova-server`, the `linux-x64` Retail
  build, owned by root (mode 755). It needs glibc 2.34 and GCC 13's
  libstdc++ at most; the machine has glibc 2.39.
- **Service:** `/etc/systemd/system/stellanova-server.service`, enabled.
  - Hardened: a dynamic user, a read-only system, no capabilities.
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
