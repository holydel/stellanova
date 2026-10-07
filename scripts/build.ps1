# Builds one preset of Stella Nova in one configuration, configuring it first
# if needed, in the Visual Studio developer environment. Shaders and packs
# are made by the pith tool of pith's own Windows build (PH_HOST_PITH), which
# this builds first.
# The android "preset" builds the APK through pith's Gradle project (its
# pack from the windows-msvc build's content map, configured first).
#   scripts/build.ps1 windows-msvc -Config Dev
#   scripts/build.ps1 web -Target stellanova
#   scripts/build.ps1 android
param(
	[Parameter(Mandatory)][string]$Preset,
	[string]$Config = 'Dev',
	[string]$Target = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pith = Join-Path (Split-Path -Parent $root) 'pith'
$buildDir = Join-Path $root "build/$Preset"
. (Join-Path $pith 'scripts/vs-env.ps1')

& (Join-Path $pith 'scripts/build.ps1') windows-msvc -Config Dev -Target pith
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($Preset -eq 'android') {
	& $PSCommandPath windows-msvc -Config Dev -Target sn_sim
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
	. (Join-Path $PSScriptRoot 'android.ps1')
	# The menu's splash, where its image is (client/CMakeLists.txt).
	$extra = @()
	if (Test-Path (Join-Path $root 'content/generated/splash/menu.png')) {
		$extra += "$(Join-Path $root 'content/art.json')=stellanova-art.pak"
	}
	& (Join-Path $pith 'scripts/android-build.ps1') stellanova -Config $Config -Root $root `
		-AppId $AndroidAppId -Label $AndroidLabel -Pack (Join-Path $root 'content/initial.json') `
		-ExtraPacks $extra
	exit $LASTEXITCODE
}

if (-not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
	Push-Location $root
	try { cmake --preset $Preset } finally { Pop-Location }
	if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
$targetArgs = if ($Target) { @('--target', $Target) } else { @() }
cmake --build $buildDir --config $Config @targetArgs
exit $LASTEXITCODE
