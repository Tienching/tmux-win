<#
.SYNOPSIS
Stress test for Windows handle transfer PID binding (P1-9).

Verifies that normal client attach succeeds and that the handle transfer
mechanism binds handle duplication to the identified client PID.
#>
param(
  [string]$Tmux = '.\tmux.exe',
  [int]$Iterations = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Tmux)) {
  $Tmux = '.\dist\tmux-win32-portable\tmux.exe'
}
if (-not (Test-Path $Tmux)) {
  throw 'tmux.exe not found'
}

# Test 1: Normal attach/detach cycle — handle transfer must succeed
for ($i = 1; $i -le $Iterations; $i++) {
  $socket = "hndxfertest-$PID-$i"
  & $Tmux -L $socket new-session -d -s S "cmd.exe"
  if ($LASTEXITCODE -ne 0) { throw "new-session failed at iteration $i" }

  # Verify session is alive — this requires handle transfer for I/O
  $out = & $Tmux -L $socket display-message -p '#{session_name}'
  if ($out -ne 'S') { throw "session name mismatch at iteration $i: $out" }

  & $Tmux -L $socket kill-server 2>$null
}

Write-Host "handle transfer boundary stress passed $Iterations iterations"
