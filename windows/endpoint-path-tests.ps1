<#
.SYNOPSIS
  Test endpoint path consistency and SID isolation.
#>
param(
  [string]$Tmux = ".\tmux.exe"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$socket = "path-test-$PID"
$out = & $Tmux -L $socket display-message -p '#{socket_path}' 2>&1
if ($LASTEXITCODE -ne 0) { throw "display socket path failed: $out" }

if ($out -notmatch '\\tmux\\S-1-') {
  throw "socket path does not include SID-isolated tmux directory: $out"
}
if ($out -notmatch '\.endpoint$') {
  throw "socket path does not end in .endpoint: $out"
}

& $Tmux -L $socket kill-server 2>$null
Write-Host "endpoint path OK: $out"
