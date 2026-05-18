param(
	[string]$Tmux = "",
	[int]$Iterations = 5,
	[int]$CommandClients = 8,
	[int]$TimeoutSeconds = 60,
	[switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

$ServerName = "codex-client-stress-" + [Guid]::NewGuid().ToString("N")
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

function Start-ClientTmuxProcess([string[]]$Arguments,
    [switch]$RedirectInput) {
	$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardInput = $RedirectInput
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false

	return [System.Diagnostics.Process]::Start($psi)
}

function Invoke-ClientTmux([string[]]$Arguments,
    [int]$Timeout = $TimeoutSeconds) {
	$process = Start-ClientTmuxProcess $Arguments
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

function Assert-Contains([string]$Name, [string]$Text, [string]$Needle) {
	if ($Text -notlike "*$Needle*") {
		throw "$Name did not contain expected text: $Needle"
	}
}

function Wait-PaneContains([string]$Name, [string]$Target, [string]$Needle,
    [int]$Timeout = $TimeoutSeconds) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$capture = ""
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		$capture = (Invoke-ClientTmux @("capture-pane", "-p",
		    "-t", $Target)).Out
		if ($capture -like "*$Needle*") {
			return $capture
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected text: $Needle"
}

function Read-ControlUntil([System.Diagnostics.Process]$Process,
    [System.Threading.Tasks.Task[string]]$ReadTask,
    [System.Collections.Generic.List[string]]$Lines,
    [string]$Name, [string]$Needle, [int]$Timeout = $TimeoutSeconds) {
	$task = $ReadTask
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		if ($task.Wait(100)) {
			$line = $task.Result
			if ($null -eq $line) {
				break
			}
			$Lines.Add($line)
			$text = $Lines -join "`n"
			if ($text -like "*$Needle*") {
				return $task
			}
			$task = $Process.StandardOutput.ReadLineAsync()
		}
		if ($Process.HasExited) {
			break
		}
	}
	$text = $Lines -join "`n"
	throw "$Name did not produce expected control output: $Needle; output: $text"
}

function Wait-NoTmuxProcess([int]$Timeout = $TimeoutSeconds) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		$matches = @(Get-CimInstance Win32_Process `
		    -Filter "name = 'tmux.exe'" | Where-Object {
			    $_.CommandLine -and $_.CommandLine.Contains($ServerName)
		    })
		if ($matches.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	$remaining = @(Get-CimInstance Win32_Process `
	    -Filter "name = 'tmux.exe'" | Where-Object {
		    $_.CommandLine -and $_.CommandLine.Contains($ServerName)
	    } | ForEach-Object {
		    "{0}:{1}" -f $_.ProcessId, $_.ExecutablePath
	    })
	throw "tmux lifecycle cleanup left processes: $($remaining -join ', ')"
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null

try {
	Invoke-ClientTmux @("new-session", "-d", "-s", "lifecycle",
	    "cmd.exe") | Out-Null

	for ($i = 1; $i -le $Iterations; $i++) {
		$clients = @()
		for ($j = 1; $j -le $CommandClients; $j++) {
			$marker = "TMUX_WIN32_CMD_CLIENT_${i}_${j}"
			$process = Start-ClientTmuxProcess @("display-message",
			    "-p", "-t", "lifecycle", "${marker}:#{session_name}")
			$clients += [pscustomobject]@{
				Process = $process
				Marker = $marker
			}
		}
		foreach ($client in $clients) {
			$process = $client.Process
			if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
				try {
					$process.Kill()
				} catch {
				}
				throw "command client timed out: $($client.Marker)"
			}
			$stdout = $process.StandardOutput.ReadToEnd()
			$stderr = $process.StandardError.ReadToEnd()
			if ($process.ExitCode -ne 0) {
				throw ("command client failed {0}: {1} {2}" -f `
				    $client.Marker, $stdout, $stderr)
			}
			Assert-Contains "command client $($client.Marker)" `
			    $stdout $client.Marker
		}

		$control = Start-ClientTmuxProcess @("-C", "attach",
		    "-t", "lifecycle") -RedirectInput
		$controlLines = [System.Collections.Generic.List[string]]::new()
		$readTask = $control.StandardOutput.ReadLineAsync()
		$readTask = Read-ControlUntil $control $readTask $controlLines `
		    "control attach iteration $i" "%session-changed"
		$controlMarker = "TMUX_WIN32_CONTROL_CLIENT_$i"
		$control.StandardInput.WriteLine(
		    "display-message -p `"$controlMarker`"")
		$control.StandardInput.Flush()
		$readTask = Read-ControlUntil $control $readTask $controlLines `
		    "control command iteration $i" $controlMarker
		$clientList = (Invoke-ClientTmux @("list-clients", "-F",
		    "#{client_name}:#{client_control_mode}")).Out
		$controlClient = ($clientList -split "`r?`n" | Where-Object {
		    $_ -like "*:1"
		} | Select-Object -First 1)
		if ([string]::IsNullOrWhiteSpace($controlClient)) {
			throw "control client not found: $clientList"
		}
		$controlClientName = ($controlClient -split ":", 2)[0]
		Invoke-ClientTmux @("detach-client", "-t", $controlClientName) 5 |
		    Out-Null
		if (-not $control.WaitForExit(3000)) {
			try {
				$control.Kill()
			} catch {
			}
			throw "control client did not exit: $i"
		}
		if ($control.ExitCode -ne 0) {
			$stderr = $control.StandardError.ReadToEnd()
			throw "control client exited with $($control.ExitCode): $stderr"
		}

		$attachMarker = "TMUX_WIN32_ATTACH_CLIENT_$i"
		$attached = Start-ClientTmuxProcess @("attach", "-t",
		    "lifecycle") -RedirectInput
		Start-Sleep -Milliseconds 500
		if ($attached.HasExited) {
			$stderr = $attached.StandardError.ReadToEnd()
			throw "attached client exited early: $stderr"
		}
		$attached.StandardInput.Write("echo $attachMarker`r")
		$attached.StandardInput.Flush()
		Wait-PaneContains "attached client iteration $i" `
		    "lifecycle:0.0" $attachMarker | Out-Null
		try {
			Invoke-ClientTmux @("detach-client", "-s",
			    "lifecycle") 5 | Out-Null
		} catch {
		}
		if (-not $attached.WaitForExit(5000)) {
			try {
				$attached.Kill()
			} catch {
			}
			throw "attached client did not detach: $i"
		}
	}

	Invoke-ClientTmux @("kill-server") | Out-Null
	Wait-NoTmuxProcess

	$message = "Windows client lifecycle stress passed: iterations={0}; " +
	    "command_clients={1}"
	Write-Host ($message -f $Iterations, $CommandClients)
} finally {
	try {
		Invoke-ClientTmux @("kill-server") 5 | Out-Null
	} catch {
	}
	try {
		Wait-NoTmuxProcess 5
	} catch {
	}
	if (-not $KeepTemp) {
		$tempRoot = [System.IO.Path]::GetTempPath()
		$tempFull = [System.IO.Path]::GetFullPath($Temp)
		if ($tempFull.StartsWith($tempRoot,
		    [System.StringComparison]::OrdinalIgnoreCase) -and
		    (Test-Path -LiteralPath $tempFull)) {
			Remove-Item -LiteralPath $tempFull -Recurse -Force
		}
		if (Test-Path -LiteralPath $Endpoint) {
			Remove-Item -LiteralPath $Endpoint -Force
		}
	}
}
