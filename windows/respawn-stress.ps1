param(
	[string]$Tmux = "",
	[int]$Iterations = 20,
	[int]$TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Iterations -lt 1) {
	throw "Iterations must be at least 1"
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

function ConvertTo-WindowsArgument([string]$Argument) {
	$quote = [string][char]34
	$backslash = [string][char]92
	$needsQuotes = [string]::IsNullOrEmpty($Argument) -or
	    $Argument.IndexOfAny([char[]]@(' ', "`t", '"')) -ne -1
	if (-not $needsQuotes) {
		return $Argument
	}

	$result = $quote
	$slashes = 0
	foreach ($ch in $Argument.ToCharArray()) {
		if ($ch -eq [char]92) {
			$slashes++
			continue
		}
		if ($ch -eq [char]34) {
			$result += $backslash * ($slashes * 2 + 1)
			$result += $quote
			$slashes = 0
			continue
		}
		if ($slashes -gt 0) {
			$result += $backslash * $slashes
			$slashes = 0
		}
		$result += $ch
	}
	if ($slashes -gt 0) {
		$result += $backslash * ($slashes * 2)
	}
	$result += $quote
	return $result
}

function Invoke-RespawnTmux([string]$ServerName, [string[]]$Arguments,
    [int]$Timeout = $TimeoutSeconds) {
	$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false

	$process = [System.Diagnostics.Process]::Start($psi)
	if (-not $process.WaitForExit($Timeout * 1000)) {
		try {
			$process.Kill()
		} catch {
		}
		throw "tmux timed out: $($Arguments -join ' ')"
	}

	$stdout = $process.StandardOutput.ReadToEnd()
	$stderr = $process.StandardError.ReadToEnd()
	if ($process.ExitCode -ne 0) {
		throw @"
tmux failed: $($Arguments -join ' ')
exit code: $($process.ExitCode)
stdout:
$stdout
stderr:
$stderr
"@
	}
	return [pscustomobject]@{
		Out = $stdout
		Err = $stderr
	}
}

function Wait-PaneContains([string]$ServerName, [string]$Target,
    [string]$Needle, [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$capture = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$capture = (Invoke-RespawnTmux $ServerName @(
		    "capture-pane", "-p", "-t", $Target)).Out
		if ($capture -like "*$Needle*") {
			return $capture
		}
		Start-Sleep -Milliseconds 200
	}
	throw "pane $Target did not contain expected text: $Needle"
}

function Stop-RespawnServer([string]$ServerName) {
	try {
		Invoke-RespawnTmux $ServerName @("kill-server") 5 | Out-Null
	} catch {
	}
	$endpoint = Join-Path (Join-Path $env:LOCALAPPDATA "tmux") `
	    ($ServerName + ".endpoint")
	if (Test-Path -LiteralPath $endpoint) {
		Remove-Item -LiteralPath $endpoint -Force
	}
}

$started = [Diagnostics.Stopwatch]::StartNew()
for ($i = 1; $i -le $Iterations; $i++) {
	$iteration = [Diagnostics.Stopwatch]::StartNew()
	$serverName = "codex-respawn-" + [Guid]::NewGuid().ToString("N")
	Write-Host ("[RESPAWN] iteration {0}/{1}" -f $i, $Iterations)
	try {
		Invoke-RespawnTmux $serverName @(
		    "new-session", "-d", "-s", "smoke", "cmd.exe") | Out-Null
		Invoke-RespawnTmux $serverName @(
		    "new-window", "-d", "-t", "smoke", "-n", "respawnp",
		    "cmd.exe") | Out-Null
		Invoke-RespawnTmux $serverName @(
		    "send-keys", "-t", "smoke:respawnp.0",
		    "echo TMUX_WIN32_RESPAWN_INITIAL_$i", "Enter") | Out-Null
		Wait-PaneContains $serverName "smoke:respawnp.0" `
		    "TMUX_WIN32_RESPAWN_INITIAL_$i" | Out-Null

		Invoke-RespawnTmux $serverName @(
		    "respawn-pane", "-k", "-t", "smoke:respawnp.0",
		    "cmd.exe") | Out-Null
		Invoke-RespawnTmux $serverName @(
		    "send-keys", "-t", "smoke:respawnp.0",
		    "echo TMUX_WIN32_RESPAWN_RESTARTED_$i", "Enter") | Out-Null
		Wait-PaneContains $serverName "smoke:respawnp.0" `
		    "TMUX_WIN32_RESPAWN_RESTARTED_$i" | Out-Null
	} finally {
		Stop-RespawnServer $serverName
	}
	Write-Host ("[RESPAWN] iteration {0}/{1} passed in {2:n1}s" -f
	    $i, $Iterations, $iteration.Elapsed.TotalSeconds)
}

Write-Host ("Windows respawn stress passed: {0} iterations in {1:n1}s" -f
    $Iterations, $started.Elapsed.TotalSeconds)
