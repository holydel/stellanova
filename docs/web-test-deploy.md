# Deploying web test pages

pith's web samples can be published on the developer's server for testing on
other devices (phones, headsets, other browsers). Two are live, both Dev
builds with the ImGui diagnostics window:

| Page | App | Files in `/srv/pith-web/` |
| --- | --- | --- |
| <https://wos-observer.com/test_webgpu.html> | `hello_clear_webgpu` | `test_webgpu.html`, `.js`, `.wasm` |
| <https://wos-observer.com/test_scene.html> (since 2026-10-01) | `hello_scene_webgpu`, under the star-map sky | `test_scene.html`, `.js`, `.wasm`, `.data` |

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
2. Add their paths to the matcher. The Caddyfile has many dated backups
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

## Undo

- The scene page only: restore
  `/etc/caddy/Caddyfile.before-pith-scene-20261001T043830Z`, run
  `systemctl reload caddy`, and delete `test_scene.html` and
  `hello_scene_webgpu.*` from `/srv/pith-web/`.
- Everything: restore `/etc/caddy/Caddyfile.before-pith-webgpu-20260930T114040Z`,
  run `systemctl reload caddy`, and delete `/srv/pith-web/`.

## Notes

- Pages log to the browser console; warnings, errors and a failed exit also
  show on the page itself. Dev pages contain emrun's log relay, which stays
  off unless the URL names `localhost`.
- Nothing about this server belongs in the pith repository, which will be
  public.
