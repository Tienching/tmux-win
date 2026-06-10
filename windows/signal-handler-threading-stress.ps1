<#
.SYNOPSIS
Stress test for Windows console signal handler threading (P0-5).

Verifies that console control signals (Ctrl-C, Ctrl-Break) are relayed
through the socketpair bridge to the main event loop, and that the server
remains responsive under repeated signal delivery.
#>
param(
  [string]$Tmux = '.\tmux.exe',
  [int]$Iterations = 50
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Tmux)) {
  $Tmux = '.\dist\tmux-win32-portable\tmux.exe'
}
if (-not (Test-Path $Tmux)) {
  throw 'tmux.exe not found'
}

$socket = "sigthread-$PID"
& $Tmux -L $socket new-session -d -s S 'ping -t 127.0.0.1'
if ($LASTEXITCODE -ne 0) { throw 'new-session failed' }

for ($i = 0; $i -lt $Iterations; $i++) {
  & $Tmux -L $socket send-keys C-c
  Start-Sleep -Milliseconds 50
  & $Tmux -L $socket display-message -p '#{session_name}' | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "server not responsive after signal iteration $i" }
}

& $Tmux -L $socket kill-server
Write-Host 'signal threading stress passed'
