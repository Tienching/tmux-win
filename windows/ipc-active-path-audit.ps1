<#
.SYNOPSIS
Audits which Windows IPC path is active in the current source tree.
.DESCRIPTION
Searches for references to old-loopback-token IPC functions and new
named-pipe-daemon IPC functions across client, server, proc, and compat
sources. Reports which path is active so that review findings are not
mistakenly applied to experimental code.
#>
param(
  [string]$OutputDir = '.\dist'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$patterns = @(
  'win32_ipc_connect',
  'win32_ipc_listen',
  'win32_ipc_accept',
  'win32_daemon_connect',
  'win32_daemon_spawn_server',
  'win32_endpoint_write_atomic'
)

$searchPaths = @('client.c','server.c','proc.c','tmux.c')
$compatFiles = Get-ChildItem -Path compat -Filter '*.c' -ErrorAction SilentlyContinue
$compatHeaders = Get-ChildItem -Path compat -Filter '*.h' -ErrorAction SilentlyContinue
$allPaths = @()
foreach ($p in $searchPaths) {
  if (Test-Path $p) { $allPaths += $p }
}
foreach ($f in $compatFiles) { $allPaths += $f.FullName }
foreach ($f in $compatHeaders) { $allPaths += $f.FullName }

$results = @()
foreach ($p in $patterns) {
  $matches = Select-String -Path $allPaths -Pattern $p -SimpleMatch
  foreach ($m in $matches) {
    $results += [pscustomobject]@{
      Pattern = $p
      Path    = $m.Filename
      Line    = $m.LineNumber
      Text    = $m.Line.Trim()
    }
  }
}

$results | Sort-Object Path, Line | Format-Table -AutoSize

New-Item -ItemType Directory -Force $OutputDir | Out-Null
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
  (Join-Path $OutputDir 'ipc-active-path-audit.json'),
  ($results | ConvertTo-Json -Depth 4),
  $Utf8NoBom
)

$activeOld = @($results | Where-Object { $_.Pattern -in @('win32_ipc_connect','win32_ipc_listen','win32_ipc_accept') }).Count
$activeNew = @($results | Where-Object { $_.Pattern -in @('win32_daemon_connect','win32_daemon_spawn_server') }).Count

Write-Host "old-ipc-refs=$activeOld new-daemon-refs=$activeNew"

if ($activeOld -gt 0 -and $activeNew -gt 0) {
  Write-Warning 'Both old IPC and new daemon APIs are referenced. Review call graph before claiming endpoint risk is fixed.'
}
