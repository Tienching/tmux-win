<#
.SYNOPSIS
Stress test for Job Object breakaway in the active server startup path (P1-8).

Verifies that tmux server can start both from a normal shell and from
within a PowerShell job (which runs inside a Job Object).
#>
param(
  [string]$Tmux = '.\tmux.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Tmux)) {
  $Tmux = '.\dist\tmux-win32-portable\tmux.exe'
}
if (-not (Test-Path $Tmux)) {
  throw 'tmux.exe not found'
}

# Test 1: Direct start
$socket = "jobbreak-$PID"
& $Tmux -L $socket new-session -d -s J "echo OK"
if ($LASTEXITCODE -ne 0) { throw 'direct start failed' }
& $Tmux -L $socket kill-server

# Test 2: Start from within a PowerShell job (CI-like wrapper with Job Object)
$script = {
  param($tmuxPath, $sock)
  & $tmuxPath -L $sock new-session -d -s J "echo OK"
  if ($LASTEXITCODE -ne 0) { exit 10 }
  & $tmuxPath -L $sock kill-server
  exit 0
}
$job = Start-Job -ScriptBlock $script -ArgumentList (Resolve-Path $Tmux), "jobbreak-inner-$PID"
Wait-Job $job -Timeout 30 | Out-Null
$out = Receive-Job $job
if ($job.State -ne 'Completed') { throw "job did not complete: $($job.State) $out" }
if ($job.ChildJobs[0].JobStateInfo.Reason) { throw $job.ChildJobs[0].JobStateInfo.Reason }

Write-Host 'job breakaway start-server smoke passed'
