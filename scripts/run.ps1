# Builds the game and runs it on a target, printing its log here and
# returning its exit code: pith's run scripts with this repository's builds.
#   scripts/run.ps1                                   # Windows, Vulkan
#   scripts/run.ps1 -On web                           # WebGPU in Chrome
#   scripts/run.ps1 -On deck                          # Steam Deck, Desktop Mode, over SSH
#   scripts/run.ps1 -On android                       # an APK through pith's Gradle project
#   scripts/run.ps1 -Arguments '--frames 300 --flight'
#   scripts/run.ps1 -Screenshot shot.png              # frame 60 (--frames) as a PNG
# The game's options: pith's shell options (ph/shell/shell.h), and --flight
# (start flying, past the menu).
param(
	[ValidateSet('win32', 'web', 'deck', 'android')][string]$On = 'win32',
	[ValidateSet('Debug', 'Dev', 'Retail')][string]$Config = 'Dev',
	[string]$Arguments = '',
	[int]$TimeoutSeconds = 0, # 0: until the app exits
	[string]$Screenshot = '' # a PNG path here
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pith = Join-Path (Split-Path -Parent $root) 'pith'
. (Join-Path $PSScriptRoot 'android.ps1')
# Android builds through Gradle; its pack comes from the Windows build's
# content map, so that preset is configured first.
$preset = @{ win32 = 'windows-msvc'; web = 'web'; deck = 'linux-x64'; android = 'windows-msvc' }[$On]
& (Join-Path $PSScriptRoot 'build.ps1') $preset -Config $Config -Target stellanova | Out-Host
if ($LASTEXITCODE) { exit $LASTEXITCODE }
$buildDir = Join-Path $root "build/$preset"

$shot = ''
if ($Screenshot) {
	$shot = 'screenshot.png'
	$Arguments = "$Arguments --screenshot $shot".Trim()
}

switch ($On) {
	'win32' {
		$bin = Join-Path $buildDir "bin/$Config"
		if ($shot) { $Arguments = $Arguments.Replace("--screenshot $shot", "--screenshot $(Join-Path $bin $shot)") }
		Push-Location $bin
		try {
			$process = Start-Process -FilePath (Join-Path $bin 'stellanova.exe') -ArgumentList $Arguments `
				-NoNewWindow -PassThru
			if ($TimeoutSeconds -gt 0 -and -not $process.WaitForExit($TimeoutSeconds * 1000)) {
				$process.Kill()
				exit 124
			}
			$process.WaitForExit()
			$code = $process.ExitCode
		}
		finally { Pop-Location }
		if ($shot) { Copy-Item (Join-Path $bin $shot) $Screenshot -Force }
		exit $code
	}
	'web' {
		$pull = if ($shot) { @{ PullFile = $shot; PullTo = $Screenshot } } else { @{} }
		& (Join-Path $pith 'scripts/web-run.ps1') stellanova -Config $Config -Arguments $Arguments `
			-BuildDir $buildDir -TimeoutSeconds $TimeoutSeconds @pull
		exit $LASTEXITCODE
	}
	'android' {
		$pull = if ($shot) { @{ PullFile = $shot; PullTo = $Screenshot } } else { @{} }
		& (Join-Path $pith 'scripts/android-run.ps1') stellanova -Config $Config -Arguments $Arguments `
			-Root $root -AppId $AndroidAppId -Label $AndroidLabel -Pack (Join-Path $root 'content/initial.json') `
			-TimeoutSeconds $TimeoutSeconds @pull
		exit $LASTEXITCODE
	}
	'deck' {
		$pull = if ($shot) { @{ PullFile = $shot; PullTo = $Screenshot } } else { @{} }
		& (Join-Path $pith 'scripts/linux-run.ps1') stellanova -Config $Config -Arguments $Arguments `
			-BuildDir $buildDir -TimeoutSeconds $TimeoutSeconds @pull
		exit $LASTEXITCODE
	}
}
