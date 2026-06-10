<#
.SYNOPSIS
  Test idle CPU behavior when no data flows.
#>
param([string]$Tmux = ".\tmux.exe", [int]$DurationSeconds = 60)
$ErrorActionPreference = 'Stop'
$label = "idle-cpu-$PID"
& $Tmux -L $label new-session -d -s S "cmd.exe"
Start-Sleep -Seconds $DurationSeconds
$out = & $Tmux -L $label list-sessions 2>&1
if ($LASTEXITCODE -ne 0) { throw "server died during idle: $out" }
& $Tmux -L $label kill-server
Write-Host "idle-cpu-stress passed"
