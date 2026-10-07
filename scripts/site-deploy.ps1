# Publishes the game's website and its browser build on wos-observer.com
# (docs/adr/0015-website.md, docs/web-test-deploy.md): the site's page as
# stellanova.html with its style and script, the game's page as
# stellanova-play.html with its .js, .wasm and .data, and the catalog and the
# English strings that the site reads, copied from content/, and the admin
# page (ADR 0016: Caddy lets only WOS Observer's superusers reach it, so its
# paths answer others with a redirect to that site's login). The live files
# go to /root/backups/stellanova-web-<UTC time>/ first; the new ones go up
# under temporary names and are moved into place together, so that nobody
# loads a new page with an old .wasm. Caddy must list every path (the
# @pith_webgpu matcher): docs/web-test-deploy.md.
#   scripts/site-deploy.ps1              # builds the web build first (Dev)
#   scripts/site-deploy.ps1 -SkipBuild   # what build/web holds already
#   scripts/site-deploy.ps1 -SiteOnly    # the site, not the game
param(
	[string]$Config = 'Dev',
	[switch]$SkipBuild,
	[switch]$SiteOnly
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$key = 'C:\Users\holyd\.ssh\wos_observer'
$server = 'root@178.128.248.85'
$web = '/srv/pith-web'

$site = Join-Path $root 'site'
$files = [ordered]@{
	'stellanova.html'           = Join-Path $site 'stellanova.html'
	'stellanova-site.css'       = Join-Path $site 'stellanova-site.css'
	'stellanova-site.js'        = Join-Path $site 'stellanova-site.js'
	'stellanova-catalog.json'   = Join-Path $root 'content/catalog.json'
	'stellanova-strings-en.txt' = Join-Path $root 'content/strings/en.txt'
	'stellanova-admin.html'     = Join-Path $site 'stellanova-admin.html'
	'stellanova-admin.js'       = Join-Path $site 'stellanova-admin.js'
}
$splash = Join-Path $site 'stellanova-splash.jpg'
if (Test-Path $splash) { $files['stellanova-splash.jpg'] = $splash }
if (-not $SiteOnly) {
	if (-not $SkipBuild) {
		& (Join-Path $PSScriptRoot 'build.ps1') web -Config $Config -Target stellanova
		if ($LASTEXITCODE) { exit $LASTEXITCODE }
	}
	$bin = Join-Path $root "build/web/bin/$Config"
	$files['stellanova-play.html'] = Join-Path $bin 'stellanova.html'
	foreach ($name in 'stellanova.js', 'stellanova.wasm', 'stellanova.data') {
		$files[$name] = Join-Path $bin $name
	}
}
foreach ($entry in $files.GetEnumerator()) {
	if (-not (Test-Path $entry.Value)) { throw "missing: $($entry.Value)" }
}

$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$backup = "/root/backups/stellanova-web-$stamp"
ssh -i $key $server "mkdir -p $backup && cp -p $web/stellanova* $backup/ 2>/dev/null; ls $backup"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
# A dropped connection on the 18 MB .data happens: each file gets three tries.
foreach ($entry in $files.GetEnumerator()) {
	for ($try = 1; $try -le 3; ++$try) {
		scp -q -i $key $entry.Value "${server}:$web/.upload-$($entry.Key)"
		if (-not $LASTEXITCODE) { break }
		Write-Host "site-deploy: $($entry.Key) failed (try $try of 3)"
	}
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
$moves = ($files.Keys | ForEach-Object {
		"chown root:caddy $web/.upload-$_ && chmod 640 $web/.upload-$_ && mv $web/.upload-$_ $web/$_"
	}) -join ' && '
ssh -i $key $server $moves
if ($LASTEXITCODE) { exit $LASTEXITCODE }

# Each path as the world sees it: 200, or Caddy does not list it yet (the
# admin page's: 302, to the login).
$paths = ($files.Keys | ForEach-Object { "/$_" }) -join ' '
ssh -i $key $server "for p in $paths; do curl -s -o /dev/null -w `"%{http_code} `$p\n`" https://wos-observer.com`$p; done"
Write-Host "site-deploy: done; the old files are in $backup"
