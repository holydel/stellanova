# Checks the game like pith's check.ps1 checks pith: formatting (clang-format,
# pith's style), every preset built in Debug, Dev and Retail, and the tests:
# natively, in Node for the web, on the Steam Deck over SSH (skipped when it
# does not answer). Run it before every commit.
#   scripts/check.ps1
#   scripts/check.ps1 -Presets windows-msvc,web
param(
	[string[]]$Presets = @('windows-msvc', 'windows-clang', 'web', 'linux-x64'),
	[string[]]$Configs = @('Debug', 'Dev', 'Retail')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$pith = Join-Path (Split-Path -Parent $root) 'pith'
$ClangFormatMajor = 23
. (Join-Path $pith 'scripts/vs-env.ps1')

function Find-ClangFormat {
	$candidates = @(Get-Command clang-format.exe -All -ErrorAction SilentlyContinue | ForEach-Object Source)
	$candidates += Join-Path $env:VSINSTALLDIR 'VC\Tools\Llvm\x64\bin\clang-format.exe'
	foreach ($candidate in $candidates) {
		if ((Test-Path $candidate) -and ((& $candidate --version) -match "version $ClangFormatMajor\.")) {
			return $candidate
		}
	}
	throw "clang-format $ClangFormatMajor not found: install LLVM $ClangFormatMajor or put it on PATH."
}

function Invoke-Step([string]$Name, [scriptblock]$Command) {
	$output = & $Command 2>&1
	if ($LASTEXITCODE -ne 0) {
		$output | Out-Host
		Write-Host "FAIL $Name"
		return $false
	}
	Write-Host "ok   $Name"
	return $true
}

$failures = 0
$started = Get-Date
Push-Location $root
try {
	$clangFormat = Find-ClangFormat
	# Tracked files deleted but not yet staged are still listed: skip them.
	$sources = git ls-files --cached --others --exclude-standard -- '*.h' '*.cpp' |
		Where-Object { Test-Path $_ }
	if (-not (Invoke-Step 'format' { & $clangFormat --dry-run --Werror --style=file $sources })) {
		$failures++
	}

	$linuxTarget = $null
	if ($Presets -contains 'linux-x64') {
		. (Join-Path $pith 'scripts/linux-common.ps1')
		if (Test-LinuxTarget (Get-LinuxTarget)) { $linuxTarget = Get-LinuxTarget }
	}
	foreach ($preset in $Presets) {
		if ($preset -eq 'web' -and -not $env:EMSDK) {
			Write-Host "skip $preset (EMSDK is not set)"
			continue
		}
		$buildDir = Join-Path $root "build/$preset"
		if (-not (Invoke-Step "$preset configure" { & (Join-Path $PSScriptRoot 'build.ps1') $preset -Config Dev -Target sn_sim })) {
			$failures++
			continue
		}
		foreach ($config in $Configs) {
			if (-not (Invoke-Step "$preset $config build" { cmake --build $buildDir --config $config })) {
				$failures++
				continue
			}
			if ($preset -eq 'linux-x64' -and -not $linuxTarget) {
				Write-Host "skip $preset $config tests (no SSH to $(Get-LinuxTarget))"
				continue
			}
			if (-not (Invoke-Step "$preset $config tests" { ctest --test-dir $buildDir -C $config --output-on-failure })) {
				$failures++
			}
		}
	}
}
finally {
	Pop-Location
}
$seconds = [int]((Get-Date) - $started).TotalSeconds
if ($failures) {
	Write-Host "check: $failures step(s) FAILED ($seconds s)"
	exit 1
}
Write-Host "check: ok ($seconds s)"
