param(
	[string]$Tmux = "",
	[int]$DurationSeconds = 30,
	[int]$ReattachCycles = 2,
	[int]$TimeoutSeconds = 60,
	[switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($DurationSeconds -lt 10) {
	throw "DurationSeconds must be at least 10"
}
if ($ReattachCycles -lt 0) {
	throw "ReattachCycles must not be negative"
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

$ServerName = "codex-console-soak-" + [Guid]::NewGuid().ToString("N")
$SessionName = "consolesoak"
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

function Invoke-ConsoleSoakTmux([string[]]$Arguments,
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

function Get-ConsoleSoakTmuxProcesses {
	$escaped = $ServerName.Replace("'", "''")
	return @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object { $_.CommandLine -like "*$escaped*" })
}

function Stop-ConsoleSoakProcesses {
	Get-ConsoleSoakTmuxProcesses | ForEach-Object {
		try {
			Stop-Process -Id $_.ProcessId -Force
		} catch {
		}
	}
}

function Stop-ConsoleSoakServer {
	try {
		Invoke-ConsoleSoakTmux @("kill-server") 10 | Out-Null
	} catch {
	}
	Start-Sleep -Milliseconds 500
	Stop-ConsoleSoakProcesses
}

function Wait-FileContains([string]$Name, [string]$Path, [string]$Needle,
    [int]$Timeout = 12000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			$content = Get-Content -LiteralPath $Path -Raw
			if ($content -like "*$Needle*") {
				return $content
			}
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected text: $Needle"
}

function Wait-PaneContains([string]$Name, [string]$Needle,
    [int]$Timeout = 12000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$capture = (Invoke-ConsoleSoakTmux @("capture-pane", "-p",
		    "-t", "${SessionName}:0.0")).Out
		if ($capture -like "*$Needle*") {
			return $capture
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected pane text: $Needle"
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null

try {
	$startedFile = Join-Path $Temp "console-started.txt"
	$inputFile = Join-Path $Temp "console-input.txt"
	$exitFile = Join-Path $Temp "console-exit.txt"
	$sizeFile = Join-Path $Temp "console-size.txt"
	$resizedFile = Join-Path $Temp "console-resized.txt"
	$resizeLog = Join-Path $Temp "console-resize-log.txt"
	$ctrlCFile = Join-Path $Temp "console-ctrlc-sent.txt"
	$ctrlBreakFile = Join-Path $Temp "console-ctrlbreak-sent.txt"
	$captureFile = Join-Path $Temp "console-pane-output.txt"
	$captureTarget = $captureFile.Replace('\', '/')
	$marker = "TMUX_WIN32_CONSOLE_SOAK_ATTACH"
	$ctrlCMarker = "TMUX_WIN32_CONSOLE_SOAK_CTRL_C"
	$ctrlBreakMarker = "TMUX_WIN32_CONSOLE_SOAK_CTRL_BREAK"
	$resizeMarker = "TMUX_WIN32_CONSOLE_SOAK_RESIZE"
	$churnPrefix = "TMUX_WIN32_CONSOLE_SOAK_CHURN_"
	$probe = Join-Path $PSScriptRoot "console-attach-probe.ps1"
	$sizes = @("94x28", "86x25", "90x27", "88x26", "96x28")
	$sequenceCount = [Math]::Max(3, [Math]::Floor($DurationSeconds / 2))
	$sequence = for ($i = 0; $i -lt $sequenceCount; $i++) {
		$sizes[$i % $sizes.Count]
	}
	$resizeTimeout = [Math]::Max(30000, 9000 + ($sequenceCount * 3000))

	Invoke-ConsoleSoakTmux @("new-session", "-d", "-s", $SessionName,
	    "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 700
	Invoke-ConsoleSoakTmux @("pipe-pane", "-t", "${SessionName}:0.0",
	    "more > `"$captureTarget`"") | Out-Null

	$process = Start-Process -FilePath powershell.exe `
	    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $probe, "-Tmux", $Tmux, "-ServerName", $ServerName,
	    "-Session", $SessionName, "-Marker", $marker, "-StartedFile",
	    $startedFile, "-InputFile", $inputFile, "-ExitFile", $exitFile,
	    "-SizeFile", $sizeFile, "-ResizeWidth", "88", "-ResizeHeight",
	    "26", "-ResizedFile", $resizedFile, "-ResizeMarker",
	    $resizeMarker, "-CtrlCCommand",
	    "`"timeout /t 30 /nobreak`"", "-CtrlCFile", $ctrlCFile,
	    "-CtrlCMarker", $ctrlCMarker, "-CtrlBreakCommand",
	    "`"timeout /t 30 /nobreak`"", "-CtrlBreakFile",
	    $ctrlBreakFile, "-CtrlBreakMarker", $ctrlBreakMarker,
	    "-ResizeSequence", ($sequence -join ","), "-ResizeLogFile",
	    $resizeLog, "-ResizeMarkerPrefix", $churnPrefix) `
	    -WindowStyle Hidden -PassThru

	Wait-FileContains "console attach input" $inputFile "sent" 15000 |
	    Out-Null
	Wait-FileContains "console attach size" $sizeFile "x" 15000 |
	    Out-Null
	Wait-FileContains "console attach Ctrl+C" $ctrlCFile "sent" 20000 |
	    Out-Null
	Wait-FileContains "console attach Ctrl+Break" $ctrlBreakFile "sent" `
	    30000 | Out-Null

	Wait-FileContains "console resize log" $resizeLog `
	    "$($churnPrefix)$($sequenceCount - 1)" $resizeTimeout | Out-Null
	$resizeLines = @(Get-Content -LiteralPath $resizeLog)
	if ($resizeLines.Count -lt $sequenceCount) {
		throw "console resize log too short: $($resizeLines.Count)"
	}
	$lastResize = $resizeLines[-1]
	if ($lastResize -notmatch "^[0-9]+:([^:]+):") {
		throw "invalid resize log line: $lastResize"
	}
	$lastSize = $Matches[1]
	$clients = (Invoke-ConsoleSoakTmux @("list-clients", "-F",
	    "#{client_session}:#{client_width}x#{client_height}")).Out
	if ($clients -notlike "*${SessionName}:$lastSize*") {
		throw "console client did not reach final size: $lastSize"
	}
	Invoke-ConsoleSoakTmux @("pipe-pane", "-t", "${SessionName}:0.0") |
	    Out-Null
	Start-Sleep -Milliseconds 700
	$captureText = Wait-FileContains "console attach pane output" `
	    $captureFile "$($churnPrefix)$($sequenceCount - 1)" 20000
	if ($captureText -notlike "*$marker*" -or
	    $captureText -notlike "*$ctrlCMarker*" -or
	    $captureText -notlike "*$ctrlBreakMarker*" -or
	    $captureText -notlike "*$resizeMarker*" -or
	    $captureText -notlike "*$($churnPrefix)$($sequenceCount - 1)*") {
		throw "console attach soak markers missing"
	}

	try {
		Invoke-ConsoleSoakTmux @("detach-client", "-s", $SessionName) 10 |
		    Out-Null
	} catch {
	}
	if (-not $process.WaitForExit(15000)) {
		$process.Kill()
		throw "console attach soak probe did not exit"
	}
	if ($process.ExitCode -ne 0) {
		throw "console attach soak probe exited with $($process.ExitCode)"
	}
	Wait-FileContains "console attach exit" $exitFile "0" 5000 |
	    Out-Null

	for ($cycle = 0; $cycle -lt $ReattachCycles; $cycle++) {
		$reattachMarker =
		    "TMUX_WIN32_CONSOLE_SOAK_REATTACH_$cycle"
		$reattachStarted = Join-Path $Temp `
		    ("console-reattach-$cycle-started.txt")
		$reattachInput = Join-Path $Temp `
		    ("console-reattach-$cycle-input.txt")
		$reattachExit = Join-Path $Temp `
		    ("console-reattach-$cycle-exit.txt")
		$reattachSize = Join-Path $Temp `
		    ("console-reattach-$cycle-size.txt")

		$reattachProcess = Start-Process -FilePath powershell.exe `
		    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass",
		    "-File", $probe, "-Tmux", $Tmux, "-ServerName",
		    $ServerName, "-Session", $SessionName, "-Marker",
		    $reattachMarker, "-StartedFile", $reattachStarted,
		    "-InputFile", $reattachInput, "-ExitFile", $reattachExit,
		    "-SizeFile", $reattachSize) -WindowStyle Hidden -PassThru
		Wait-FileContains "console reattach input" $reattachInput `
		    "sent" 15000 | Out-Null
		Wait-FileContains "console reattach size" $reattachSize `
		    "x" 15000 | Out-Null
		Wait-PaneContains "console reattach pane output" `
		    $reattachMarker 15000 | Out-Null
		try {
			Invoke-ConsoleSoakTmux @("detach-client", "-s",
			    $SessionName) 10 | Out-Null
		} catch {
		}
		if (-not $reattachProcess.WaitForExit(15000)) {
			$reattachProcess.Kill()
			throw "console reattach probe did not exit"
		}
		if ($reattachProcess.ExitCode -ne 0) {
			throw ("console reattach probe exited with {0}" -f
			    $reattachProcess.ExitCode)
		}
		Wait-FileContains "console reattach exit" $reattachExit `
		    "0" 5000 | Out-Null
	}

	$rawScript = Join-Path $Temp "console-raw-ctrlc.ps1"
	$rawReady = Join-Path $Temp "console-raw-ctrlc-ready.txt"
	$rawOutput = Join-Path $Temp "console-raw-ctrlc-etx.txt"
	$rawStarted = Join-Path $Temp "console-raw-ctrlc-started.txt"
	$rawInput = Join-Path $Temp "console-raw-ctrlc-input.txt"
	$rawExit = Join-Path $Temp "console-raw-ctrlc-exit.txt"
	$rawSize = Join-Path $Temp "console-raw-ctrlc-size.txt"
	$rawSent = Join-Path $Temp "console-raw-ctrlc-sent.txt"
	Set-Content -LiteralPath $rawScript -Encoding ascii -Value @'
param([string]$Ready, [string]$Output)
$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class TmuxCtrlHandler {
	public delegate bool ConsoleCtrlDelegate(uint type);
	public static ConsoleCtrlDelegate Handler = new ConsoleCtrlDelegate(Ignore);
	public static bool Ignore(uint type) { return true; }
	[DllImport("kernel32.dll")]
	public static extern bool SetConsoleCtrlHandler(
	    ConsoleCtrlDelegate handler, bool add);
}
"@
Add-Type -TypeDefinition $source
[void][TmuxCtrlHandler]::SetConsoleCtrlHandler(
    [TmuxCtrlHandler]::Handler, $true)
[Console]::TreatControlCAsInput = $true
Set-Content -LiteralPath $Ready -Encoding ascii -Value "ready"
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([Console]::KeyAvailable) {
		$key = [Console]::ReadKey($true)
		if ([int][char]$key.KeyChar -eq 3) {
			Set-Content -LiteralPath $Output -Encoding ascii `
			    -Value "TMUX_WIN32_ETX_BYTE"
			exit 0
		}
	}
	Start-Sleep -Milliseconds 50
}
Set-Content -LiteralPath $Output -Encoding ascii `
    -Value "TMUX_WIN32_ETX_MISSING"
exit 2
'@
	$rawCommand = "powershell -NoProfile -NonInteractive " +
	    "-ExecutionPolicy Bypass -File `"$rawScript`" " +
	    "-Ready `"$rawReady`" -Output `"$rawOutput`""
	Invoke-ConsoleSoakTmux @("send-keys", "-t", "${SessionName}:0.0",
	    $rawCommand, "Enter") | Out-Null
	Wait-FileContains "console raw Ctrl+C ready" $rawReady "ready" `
	    15000 | Out-Null

	$rawProcess = Start-Process -FilePath powershell.exe `
	    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $probe, "-Tmux", $Tmux, "-ServerName", $ServerName,
	    "-Session", $SessionName, "-Marker",
	    "TMUX_WIN32_CONSOLE_SOAK_RAW_CTRL_C_ATTACH", "-StartedFile",
	    $rawStarted, "-InputFile", $rawInput, "-ExitFile", $rawExit,
	    "-SizeFile", $rawSize, "-SkipInitialInput", "-CtrlCFile",
	    $rawSent) -WindowStyle Hidden -PassThru
	Wait-FileContains "console raw Ctrl+C sent" $rawSent "sent" `
	    15000 | Out-Null
	Wait-FileContains "console raw Ctrl+C ETX" $rawOutput `
	    "TMUX_WIN32_ETX_BYTE" 15000 | Out-Null
	try {
		Invoke-ConsoleSoakTmux @("detach-client", "-s", $SessionName) 10 |
		    Out-Null
	} catch {
	}
	if (-not $rawProcess.WaitForExit(15000)) {
		$rawProcess.Kill()
		throw "console raw Ctrl+C probe did not exit"
	}
	if ($rawProcess.ExitCode -ne 0) {
		throw "console raw Ctrl+C probe exited with $($rawProcess.ExitCode)"
	}
	Wait-FileContains "console raw Ctrl+C exit" $rawExit "0" 5000 |
	    Out-Null

	Write-Host ("Windows console attach soak passed: {0}s, {1} resizes, {2} reattach cycles, raw Ctrl+C" -f
	    $DurationSeconds, $sequenceCount, $ReattachCycles)
} finally {
	Stop-ConsoleSoakServer
	if (Test-Path -LiteralPath $Endpoint) {
		Remove-Item -LiteralPath $Endpoint -Force
	}
	if (-not $KeepTemp -and (Test-Path -LiteralPath $Temp)) {
		Remove-Item -LiteralPath $Temp -Recurse -Force
	}
}
