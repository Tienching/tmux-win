<#
.SYNOPSIS
  Stress-test stdio bridge close lifecycle.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$Iterations = 100
)

$ErrorActionPreference = 'Stop'
for ($i = 1; $i -le $Iterations; $i++) {
  $label = "stdio-$PID-$i"
  "echo STDIO_$i`nexit`n" | & $Tmux -L $label new-session -d
  & $Tmux -L $label kill-server 2>$null
}
Write-Host "stdio-bridge-close-stress passed $Iterations iterations"
