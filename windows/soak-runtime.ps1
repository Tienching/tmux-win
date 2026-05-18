param(
	[string]$Tmux = "",
	[int]$DurationSeconds = 20,
	[int]$TimeoutSeconds = 10,
	[switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($DurationSeconds -lt 5) {
	throw "DurationSeconds must be at least 5"
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

$ServerName = "codex-soak-" + [Guid]::NewGuid().ToString("N")
$Temp = Join-Path ([System.IO.Path]::GetTempPath()) $ServerName
$Endpoint = Join-Path (Join-Path $env:LOCALAPPDATA "tmux") `
    ($ServerName + ".endpoint")

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

function Invoke-SoakTmux([string[]]$Arguments,
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

function Start-SoakTmuxProcess([string[]]$Arguments) {
	$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	return [System.Diagnostics.Process]::Start($psi)
}

function Get-SoakTmuxProcesses {
	$escaped = $ServerName.Replace("'", "''")
	return @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object { $_.CommandLine -like "*$escaped*" })
}

function Stop-SoakProcesses {
	Get-SoakTmuxProcesses | ForEach-Object {
		try {
			Stop-Process -Id $_.ProcessId -Force
		} catch {
		}
	}
}

function Stop-SoakServer([int]$Timeout = 60000) {
	$process = Start-SoakTmuxProcess @("kill-server")
	if ($process.WaitForExit($Timeout)) {
		$stdout = $process.StandardOutput.ReadToEnd()
		$stderr = $process.StandardError.ReadToEnd()
		if ($process.ExitCode -ne 0) {
			throw ("kill-server exited with {0}: {1} {2}" -f `
			    $process.ExitCode, $stdout, $stderr)
		}
	} else {
		try {
			$process.Kill()
		} catch {
		}
	}

	$remaining = @()
	$wait = [Diagnostics.Stopwatch]::StartNew()
	while ($wait.ElapsedMilliseconds -lt 5000) {
		$remaining = @(Get-SoakTmuxProcesses)
		if ($remaining.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	Stop-SoakProcesses
	$wait.Restart()
	while ($wait.ElapsedMilliseconds -lt 5000) {
		$remaining = @(Get-SoakTmuxProcesses)
		if ($remaining.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	$details = ($remaining | ForEach-Object {
	    "{0}:{1}" -f $_.ProcessId, $_.CommandLine
	}) -join "; "
	throw "kill-server timed out; remaining tmux processes: $details"
}

function Wait-FileContains([string]$Name, [string]$Path, [string]$Needle,
    [int]$Timeout = 12000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$text = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			$text = Get-Content -LiteralPath $Path -Raw
			if ($text -like "*$Needle*") {
				return $text
			}
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected text: $Needle"
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null
$pipeFile = Join-Path $Temp "soak-pipe.txt"
$jobFile = Join-Path $Temp "soak-jobs.txt"
$pipeTarget = $pipeFile.Replace('\', '/')
$jobTarget = $jobFile.Replace('\', '/')

try {
	Invoke-SoakTmux @("new-session", "-d", "-s", "soak", "cmd.exe") |
	    Out-Null
	Start-Sleep -Milliseconds 700
	Invoke-SoakTmux @("split-window", "-h", "-t", "soak:0.0",
	    "cmd.exe") | Out-Null
	Invoke-SoakTmux @("split-window", "-v", "-t", "soak:0.1",
	    "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 700

	Invoke-SoakTmux @("pipe-pane", "-t", "soak:0.0",
	    "more > $pipeTarget") | Out-Null

	$started = [Diagnostics.Stopwatch]::StartNew()
	$iteration = 0
	while ($started.Elapsed.TotalSeconds -lt $DurationSeconds) {
		$iteration++
		for ($pane = 0; $pane -lt 3; $pane++) {
			Invoke-SoakTmux @("send-keys", "-t",
			    "soak:0.$pane",
			    "echo TMUX_WIN32_SOAK_${iteration}_$pane",
			    "Enter") | Out-Null
		}
		Invoke-SoakTmux @("run-shell", "-b",
		    "echo TMUX_WIN32_SOAK_JOB_$iteration>>$jobTarget") |
		    Out-Null
		if (($iteration % 2) -eq 0) {
			$width = 48 + ($iteration % 16)
			Invoke-SoakTmux @("resize-pane", "-t", "soak:0.0",
			    "-x", [string]$width) | Out-Null
		}
		if (($iteration % 3) -eq 0) {
			Invoke-SoakTmux @("capture-pane", "-p", "-t",
			    "soak:0.0") | Out-Null
		}
		Start-Sleep -Milliseconds 400
	}

	$finalPipe = "TMUX_WIN32_SOAK_FINAL_$iteration"
	Invoke-SoakTmux @("send-keys", "-t", "soak:0.0",
	    "echo $finalPipe", "Enter") | Out-Null
	Start-Sleep -Milliseconds 1000
	Invoke-SoakTmux @("pipe-pane", "-t", "soak:0.0") | Out-Null
	Start-Sleep -Milliseconds 700
	$pipeText = Get-Content -LiteralPath $pipeFile -Raw
	if ($pipeText -notlike "*$finalPipe*") {
		throw "soak pipe-pane did not contain expected text: $finalPipe"
	}
	if (([regex]::Matches($pipeText, "TMUX_WIN32_SOAK_")).Count -lt 5) {
		throw "soak pipe-pane captured too few markers"
	}

	Wait-FileContains "soak run-shell jobs" $jobFile `
	    "TMUX_WIN32_SOAK_JOB_$iteration" | Out-Null
	$panes = (Invoke-SoakTmux @("list-panes", "-t", "soak:0",
	    "-F", "#{pane_index}:#{pane_dead}:#{pane_current_command}")).Out
	foreach ($pane in @("0", "1", "2")) {
		if ($panes -notlike "*${pane}:0:*") {
			throw "soak pane $pane is missing or dead: $panes"
		}
	}

	Stop-SoakServer
	Write-Host ("Windows runtime soak passed: {0:n1}s, {1} iterations" -f
	    $started.Elapsed.TotalSeconds, $iteration)
} finally {
	try {
		Stop-SoakServer 3000
	} catch {
	}
	Stop-SoakProcesses
	if (Test-Path -LiteralPath $Endpoint) {
		Remove-Item -LiteralPath $Endpoint -Force
	}
	if (-not $KeepTemp -and (Test-Path -LiteralPath $Temp)) {
		Remove-Item -LiteralPath $Temp -Recurse -Force
	}
}
