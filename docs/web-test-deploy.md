# Deploying web test pages

pith's web samples can be published on the developer's server for testing on
other devices (phones, headsets, other browsers). The first one is live at
<https://wos-observer.com/test_webgpu.html> (`hello_clear_webgpu`, Dev, with
the ImGui diagnostics window).

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
upload the three files. The page is renamed; the `.js` and `.wasm` keep their
names, because the page and the script refer to them.

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

Expect 200, with `application/wasm` for the `.wasm`.

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

Restore `/etc/caddy/Caddyfile.before-pith-webgpu-20260930T114040Z`, run
`systemctl reload caddy`, and delete `/srv/pith-web/`.

## Notes

- Pages log to the browser console; warnings, errors and a failed exit also
  show on the page itself. Dev pages contain emrun's log relay, which stays
  off unless the URL names `localhost`.
- Nothing about this server belongs in the pith repository, which will be
  public.
