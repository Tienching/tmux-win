<#
.SYNOPSIS
  Stress-test stale endpoint cleanup.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$Iterations = 20
)

$ErrorActionPreference = 'Stop'
for ($i = 1; $i -le $Iterations; $i++) {
  $label = "stale-$PID-$i"
  & $Tmux -L $label new-session -d -s S "cmd.exe"
  & $Tmux -L $label display-message -p "#{session_name}" | Out-Null
  # Kill server hard, leave endpoint file
  & $Tmux -L $label kill-server 2>$null
  Start-Sleep -Milliseconds 100
  # New client should handle stale endpoint
  $out = & $Tmux -L $label new-session -d -s S2 "cmd.exe" 2>&1
  & $Tmux -L $label kill-server 2>$null
}
Write-Host "endpoint-stale-stress passed $Iterations iterations"
