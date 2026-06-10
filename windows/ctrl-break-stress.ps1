<#
.SYNOPSIS
  Stress-test Ctrl-Break serialization under concurrent pane signals.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$Panes = 4,
  [int]$Iterations = 50
)

$ErrorActionPreference = 'Stop'
$label = "ctrlbreak-$PID"
& $Tmux -L $label new-session -d -s S "cmd.exe"
for ($p = 1; $p -lt $Panes; $p++) {
  & $Tmux -L $label split-window -t S "cmd.exe"
}

for ($i = 1; $i -le $Iterations; $i++) {
  for ($p = 0; $p -lt $Panes; $p++) {
    & $Tmux -L $label send-keys -t "S:0.$p" C-c
  }
}

& $Tmux -L $label list-panes -a
& $Tmux -L $label kill-server
Write-Host "ctrl-break-stress passed"
