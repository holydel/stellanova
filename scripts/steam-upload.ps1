# Uploads the game's Retail builds to Steam with SteamPipe (docs/steam.md): a
# Windows x64 depot and a Linux + SteamOS depot of app 1096260, through the
# SteamCMD in pith's private Steamworks SDK, as the build account. Builds
# first; the upload is not set live unless -SetLive names a beta branch: set
# it live in Steamworks (SteamPipe > Builds), where the default branch is.
# The account logs in once by hand (password and Steam Guard); SteamCMD then
# remembers it:
#   <sdk>\tools\ContentBuilder\builder\steamcmd.exe +login starire +quit
# Then:
#   scripts/steam-upload.ps1
#   scripts/steam-upload.ps1 -Preview                # check, upload nothing
#   scripts/steam-upload.ps1 -SetLive beta -Description 'bots at half speed'
param(
	[string]$Description = '',
	[string]$SetLive = '',
	[string]$Account = 'starire',
	[switch]$Preview,
	[switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pith = Join-Path (Split-Path -Parent $root) 'pith'

$AppId = 1096260
# Each depot: where its files come from, and which ones go. The art pack
# (content/art.json: the splash, the generated models, the item pictures)
# exists where its generated files are, on the developer's PC: an upload
# from elsewhere would go without it, so it is required.
$Depots = @(
	@{ Id = 1096261; Name = 'windows'; Preset = 'windows-msvc'
		Files = @('stellanova.exe', 'stellanova.pak', 'stellanova-art.pak', 'steam_api64.dll') },
	@{ Id = 1096262; Name = 'linux'; Preset = 'linux-x64'
		Files = @('stellanova', 'stellanova.pak', 'stellanova-art.pak', 'libsteam_api.so') }
)

$sdk = Get-ChildItem (Join-Path $pith 'third_party/private') -Directory -Filter 'steamworks_sdk_*' |
	Sort-Object Name | Select-Object -Last 1
if (-not $sdk) { throw 'No Steamworks SDK in pith/third_party/private.' }
$builder = Join-Path $sdk.FullName 'sdk/tools/ContentBuilder/builder'
$steamcmd = Join-Path $builder 'steamcmd.exe'

# SteamCMD asks for a password when it has no login for the account: stop
# before it waits for one that never comes.
$config = Join-Path $builder 'config/config.vdf'
if (-not ((Test-Path $config) -and (Select-String -Path $config -SimpleMatch "`"$Account`"" -Quiet))) {
	throw "SteamCMD has no login for '$Account': run once, and answer its questions:`n  & '$steamcmd' +login $Account +quit"
}

if (-not $SkipBuild) {
	foreach ($depot in $Depots) {
		& (Join-Path $root 'scripts/build.ps1') $depot.Preset -Config Retail -Target stellanova
		if ($LASTEXITCODE -ne 0) { throw "The $($depot.Preset) Retail build failed." }
	}
}

# What goes up, copied fresh each time: nothing left over from older builds.
$steam = Join-Path $root 'build/steam'
$content = Join-Path $steam 'content'
if (Test-Path $content) { Remove-Item $content -Recurse -Force }
foreach ($depot in $Depots) {
	$from = Join-Path $root "build/$($depot.Preset)/bin/Retail"
	$to = Join-Path $content $depot.Name
	New-Item -ItemType Directory -Force $to | Out-Null
	foreach ($file in $depot.Files) {
		$path = Join-Path $from $file
		if (-not (Test-Path $path)) { throw "$path is missing." }
		Copy-Item $path $to
	}
}

if (-not $Description) {
	$commit = (& git -C $root rev-parse --short HEAD).Trim()
	$dirty = if (& git -C $root status --porcelain) { ' with uncommitted changes' } else { '' }
	$Description = "stellanova $commit$dirty, $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
}

# The app build script, its depots inline. VDF strings take no escapes, so
# Windows paths go as they are.
$output = Join-Path $steam 'output'
New-Item -ItemType Directory -Force $output | Out-Null
$lines = @(
	'"AppBuild"', '{',
	"`t`"AppID`" `"$AppId`"",
	"`t`"Desc`" `"$($Description.Replace('"', "'"))`"",
	"`t`"Preview`" `"$(if ($Preview) { 1 } else { 0 })`"",
	"`t`"ContentRoot`" `"$content`"",
	"`t`"BuildOutput`" `"$output`""
)
if ($SetLive) { $lines += "`t`"SetLive`" `"$SetLive`"" }
$lines += "`t`"Depots`"", "`t{"
foreach ($depot in $Depots) {
	$lines += "`t`t`"$($depot.Id)`"", "`t`t{", "`t`t`t`"FileMapping`"", "`t`t`t{",
		"`t`t`t`t`"LocalPath`" `"$($depot.Name)\*`"", "`t`t`t`t`"DepotPath`" `".`"",
		"`t`t`t`t`"Recursive`" `"1`"", "`t`t`t}", "`t`t}"
}
$lines += "`t}", '}'
$script = Join-Path $steam "app_build_$AppId.vdf"
Set-Content -Path $script -Value $lines -Encoding ascii

Write-Host "steam-upload: $Description$(if ($Preview) { ' (preview)' })"
& $steamcmd +login $Account +run_app_build $script +quit | Tee-Object -Variable log | Out-Host
$built = $log | Select-String -Pattern 'Successfully finished AppID \d+ build \(BuildID (\d+)\)'
if ($LASTEXITCODE -ne 0 -or -not $built) { throw 'SteamCMD did not finish the build: see its output above.' }
$buildId = $built.Matches[0].Groups[1].Value
Write-Host "steam-upload: build $buildId$(if ($SetLive) { ", live on '$SetLive'" } else { ': set it live in Steamworks, SteamPipe > Builds' })"
