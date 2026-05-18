param(
	[string]$Tmux = "",
	[int]$Iterations = 10,
	[int]$BackgroundJobs = 8,
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

$ServerName = "codex-job-stress-" + [Guid]::NewGuid().ToString("N")
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

function Invoke-JobTmux([string[]]$Arguments,
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

function Assert-Contains([string]$Name, [string]$Text, [string]$Needle) {
	if ($Text -notlike "*$Needle*") {
		throw "$Name did not contain expected text: $Needle"
	}
}

function Wait-FileMarkers([string]$Name, [string]$Path,
    [string[]]$Markers, [int]$Timeout = $TimeoutSeconds) {
	$missing = $Markers
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			try {
				$content = Get-Content -LiteralPath $Path -Raw
				$missing = @($Markers | Where-Object {
				    $content -notlike "*$_*"
				})
				if (@($missing).Count -eq 0) {
					return
				}
			} catch {
			}
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name missing markers: $($missing -join ', ')"
}

function Wait-JobOutputFiles([string]$Name, [object[]]$Outputs,
    [int]$Timeout = $TimeoutSeconds) {
	$missing = $Outputs
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		$missing = @($Outputs | Where-Object {
			if (-not (Test-Path -LiteralPath $_.Path)) {
				return $true
			}
			try {
				$content = Get-Content -LiteralPath $_.Path -Raw
				return ($content -notlike "*$($_.Marker)*")
			} catch {
				return $true
			}
		})
		if ($missing.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	$missingText = @($missing | ForEach-Object {
	    "{0} in {1}" -f $_.Marker, $_.Path
	})
	throw "$Name missing outputs: $($missingText -join ', ')"
}

function Wait-NoProcessMarker([string]$Marker, [int]$Timeout = $TimeoutSeconds) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
		$matches = @(Get-CimInstance Win32_Process |
		    Where-Object {
			    $_.CommandLine -and $_.CommandLine.Contains($Marker)
		    })
		if ($matches.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	$remaining = @(Get-CimInstance Win32_Process |
	    Where-Object {
		    $_.CommandLine -and $_.CommandLine.Contains($Marker)
	    } | ForEach-Object {
		    "{0}:{1}" -f $_.ProcessId, $_.Name
	    })
	throw "job cleanup left marked processes: $($remaining -join ', ')"
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null

try {
	Invoke-JobTmux @("new-session", "-d", "-s", "job", "cmd.exe") |
	    Out-Null

	for ($i = 1; $i -le $Iterations; $i++) {
		$lineCount = 30
		$mixedCommand = "for /l %n in (1,1,$lineCount) do @(" +
		    "echo TMUX_WIN32_JOB_STRESS_OUT_${i}_%n & " +
		    "echo TMUX_WIN32_JOB_STRESS_ERR_${i}_%n 1>&2)"
		$result = Invoke-JobTmux @("run-shell", "-E", $mixedCommand)
		$mixedOutput = $result.Out + $result.Err
		Assert-Contains "run-shell -E iteration $i" $mixedOutput `
		    "TMUX_WIN32_JOB_STRESS_OUT_${i}_1"
		Assert-Contains "run-shell -E iteration $i" $mixedOutput `
		    "TMUX_WIN32_JOB_STRESS_ERR_${i}_1"
		Assert-Contains "run-shell -E iteration $i" $mixedOutput `
		    "TMUX_WIN32_JOB_STRESS_OUT_${i}_${lineCount}"
		Assert-Contains "run-shell -E iteration $i" $mixedOutput `
		    "TMUX_WIN32_JOB_STRESS_ERR_${i}_${lineCount}"

		$outputs = @()
		for ($j = 1; $j -le $BackgroundJobs; $j++) {
			$backgroundFile = Join-Path $Temp `
			    ("background-{0}-{1}.txt" -f $i, $j)
			$backgroundTarget = $backgroundFile.Replace('\', '/')
			$marker = "TMUX_WIN32_JOB_BG_${i}_${j}"
			$outputs += [pscustomobject]@{
				Path = $backgroundFile
				Marker = $marker
			}
			Invoke-JobTmux @("run-shell", "-b",
			    "ping -n 2 127.0.0.1 >NUL & echo $marker>$backgroundTarget") |
			    Out-Null
		}
		Wait-JobOutputFiles "background jobs iteration $i" $outputs
	}

	$cancelMarker = "TMUX_WIN32_JOB_CANCEL_" +
	    [Guid]::NewGuid().ToString("N")
	$cancelFile = Join-Path $Temp "cancel.txt"
	$cancelTarget = $cancelFile.Replace('\', '/')
	$cancelCommand = "echo $cancelMarker-start>$cancelTarget & " +
	    "ping -n 60 127.0.0.1 >NUL & " +
	    "echo $cancelMarker-end>>$cancelTarget"
	Invoke-JobTmux @("run-shell", "-b", $cancelCommand) | Out-Null
	Wait-FileMarkers "background cancellation start" $cancelFile `
	    @("$cancelMarker-start")

	Invoke-JobTmux @("kill-server") | Out-Null
	Wait-NoProcessMarker $cancelMarker

	$message = "Windows job stress passed: iterations={0}; " +
	    "background_jobs={1}"
	Write-Host ($message -f $Iterations, $BackgroundJobs)
} finally {
	try {
		Invoke-JobTmux @("kill-server") 5 | Out-Null
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
