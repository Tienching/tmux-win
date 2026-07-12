<#
.SYNOPSIS
  Stress-test stale endpoint cleanup.
#>
param(
  [string]$Tmux = ".\tmux.exe",
  [int]$Iterations = 20
)

$ErrorActionPreference = 'Stop'

# A live endpoint with a temporarily unreachable pipe must be preserved. The
# client can retry later; deleting it would orphan every session in that server.
$liveLabel = "live-$PID"
& $Tmux -L $liveLabel new-session -d -s keeper "cmd.exe"
$endpoint = (& $Tmux -L $liveLabel display-message -p '#{socket_path}').Trim()
$original = [System.IO.File]::ReadAllBytes($endpoint)
try {
  $text = [System.Text.Encoding]::UTF8.GetString($original)
  $fields = $text -split "`n"
  if ($fields.Count -lt 4 -or $fields[0] -ne 'tmux-win32-ipc-v1') {
    throw "unexpected endpoint record: $endpoint"
  }
  $fields[1] = '1'
  [System.IO.File]::WriteAllBytes(
    $endpoint,
    [System.Text.Encoding]::UTF8.GetBytes(($fields -join "`n"))
  )

  $savedErrorActionPreference = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  & $Tmux -L $liveLabel new-session -d -s probe "cmd.exe" 2>$null
  $ErrorActionPreference = $savedErrorActionPreference
  if ($LASTEXITCODE -eq 0) { throw 'client unexpectedly replaced a live endpoint' }
  if (-not (Test-Path -LiteralPath $endpoint)) {
    throw 'client deleted an endpoint whose owner process is still alive'
  }
} finally {
  [System.IO.File]::WriteAllBytes($endpoint, $original)
}
& $Tmux -L $liveLabel has-session -t keeper
if ($LASTEXITCODE -ne 0) { throw 'keeper session was lost after endpoint recovery' }
& $Tmux -L $liveLabel kill-server 2>$null

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
