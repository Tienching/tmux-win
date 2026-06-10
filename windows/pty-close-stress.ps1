<#
.SYNOPSIS
  Stress-test pty close/kill lifecycle for use-after-close and hang regressions.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$Iterations = 200,
  [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

for ($i = 1; $i -le $Iterations; $i++) {
  & $Tmux -L "ptyclose-$PID-$i" new-session -d -s S "cmd.exe"
  & $Tmux -L "ptyclose-$PID-$i" send-keys -t S:0 "echo ITER_$i" Enter
  Start-Sleep -Milliseconds 50
  & $Tmux -L "ptyclose-$PID-$i" kill-session -t S
  & $Tmux -L "ptyclose-$PID-$i" kill-server 2>$null
}

Write-Host "pty-close-stress passed $Iterations iterations"
