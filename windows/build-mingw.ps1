param(
	[string]$CC = $(if ($env:CC) { $env:CC } else { "gcc" }),
	[string]$CXX = $(if ($env:CXX) { $env:CXX } else { "g++" }),
	[string]$Yacc = $(if ($env:YACC) { $env:YACC } else { "" }),
	[string]$Output = "",
	[string]$Version = "",
	[string]$LibeventCflags = "",
	[string]$LibeventLibs = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($Version)) {
	$configure = Get-Content -LiteralPath (Join-Path $Root "configure.ac") -Raw
	if ($configure -match "AC_INIT\(\[tmux\],\s*\[?([^\]\),\s]+)\]?") {
		$Version = $Matches[1]
	} else {
		$Version = "unknown"
	}
}

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path $Root $Output
}

$probe = Join-Path $PSScriptRoot "probe-mingw.ps1"
$arguments = @{
	CC = $CC
	CXX = $CXX
	UseGeneratedParser = $true
	UseSystemLibevent = $true
	Version = $Version
	OutputExe = $Output
}

if (-not [string]::IsNullOrWhiteSpace($Yacc)) {
	$arguments.Yacc = $Yacc
}
if (-not [string]::IsNullOrWhiteSpace($LibeventCflags)) {
	$arguments.LibeventCflags = $LibeventCflags
}
if (-not [string]::IsNullOrWhiteSpace($LibeventLibs)) {
	$arguments.LibeventLibs = $LibeventLibs
}

& $probe @arguments
