<#
.SYNOPSIS
  Build and run the regex-parity probe on Windows; print comparison instructions.
#>
$ErrorActionPreference = 'Stop'
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

$Probe    = Join-Path $RepoRoot 'tools\regex-parity.c'
$ShimCC   = Join-Path $RepoRoot 'compat\win32-regex.cc'
$ShimH    = Join-Path $RepoRoot 'compat\win32-regex.h'
$OutExe   = Join-Path $RepoRoot 'tools\regex-parity.exe'

# --- Build with MinGW -------------------------------------------------------
$cc = 'g++.exe'
Write-Host "Compiling with $cc ..."

& $cc -O2 -D_WIN32 -I"$RepoRoot\compat" `
    -o $OutExe $Probe $ShimCC -lstdc++

if ($LASTEXITCODE -ne 0) { throw "compile failed" }

# --- Run ---------------------------------------------------------------------
Write-Host "`n--- Windows results ---"
& $OutExe
if ($LASTEXITCODE -ne 0) {
    Write-Host "`nSome parity tests FAILED on Windows." -ForegroundColor Red
} else {
    Write-Host "`nAll parity tests passed on Windows." -ForegroundColor Green
}

# --- Linux comparison instructions -------------------------------------------
Write-Host @"

=== To compare on Linux ===

  1. Copy tools/regex-parity.c to a Linux host.
  2. Compile natively (no shim needed):

       gcc -O2 -o regex-parity tools/regex-parity.c

  3. Run:

       ./regex-parity

  4. Compare the pat/s/rc/so/eo lines with the Windows output above.
     Any differences indicate a parity gap in the win32-regex shim.

"@
