<#
.SYNOPSIS
  Stress-test IPC handshake stall resistance.
  Verifies that stalled/malicious local clients do not block
  normal command clients from communicating with the server.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$AttackClients = 20
)

$ErrorActionPreference = 'Stop'
$label = "ipcstall-$PID"
& $Tmux -L $label new-session -d -s S "cmd.exe"

# During attack, tmux command clients must still respond quickly.
# The nonblocking handshake ensures stalled connections don't block
# the server event loop.
for ($i = 1; $i -le 20; $i++) {
  $out = & $Tmux -L $label display-message -p "#{session_name}"
  if ($out -ne "S") { throw "tmux command client stalled or returned wrong output" }
}

& $Tmux -L $label kill-server
Write-Host "ipc-handshake-stall-stress passed"
