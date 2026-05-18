param(
	[string]$Tmux = "",
	[int]$Iterations = 3,
	[int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "dist\tmux-win32-portable\tmux.exe"
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

function Invoke-SignalTmux([string]$ServerName, [string[]]$Arguments,
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
	return $stdout
}

function Wait-CurrentCommand([string]$ServerName, [string]$Target,
    [string]$Expected, [int]$Timeout = 12000) {
	$watch = [Diagnostics.Stopwatch]::StartNew()
	while ($watch.ElapsedMilliseconds -lt $Timeout) {
		$current = (Invoke-SignalTmux $ServerName @(
		    "display-message", "-p", "-t", $Target,
		    "#{pane_current_command}") 10).Trim()
		if ($current -eq $Expected) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	throw "pane $Target did not reach command ${Expected}: $current"
}

function Wait-FileContains([string]$Path, [string]$Needle,
    [int]$Timeout = 12000) {
	$watch = [Diagnostics.Stopwatch]::StartNew()
	while ($watch.ElapsedMilliseconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			$content = Get-Content -LiteralPath $Path -Raw
			if ($content -like "*$Needle*") {
				return
			}
		}
		Start-Sleep -Milliseconds 200
	}
	throw "file did not contain ${Needle}: $Path"
}

function Write-CtrlBreakProbe([string]$Path) {
	Set-Content -LiteralPath $Path -Encoding ascii -Value @'
param([string]$Ready, [string]$Output)
$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class TmuxSignalMatrixBreakHandler {
	public delegate bool ConsoleCtrlDelegate(uint type);
	public static volatile int SeenBreak;
	public static ConsoleCtrlDelegate Handler =
	    new ConsoleCtrlDelegate(Handle);
	public static bool Handle(uint type) {
		if (type == 1)
			SeenBreak = 1;
		return true;
	}
	[DllImport("kernel32.dll")]
	public static extern bool SetConsoleCtrlHandler(
	    ConsoleCtrlDelegate handler, bool add);
}
"@
Add-Type -TypeDefinition $source
[void][TmuxSignalMatrixBreakHandler]::SetConsoleCtrlHandler(
    [TmuxSignalMatrixBreakHandler]::Handler, $true)
Set-Content -LiteralPath $Ready -Encoding ascii -Value "ready"
$deadline = [DateTime]::UtcNow.AddSeconds(20)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([TmuxSignalMatrixBreakHandler]::SeenBreak -ne 0) {
		Set-Content -LiteralPath $Output -Encoding ascii `
		    -Value "CTRL_BREAK"
		exit 0
	}
	Start-Sleep -Milliseconds 50
}
exit 2
'@
}

function Write-RawEtXProbe([string]$Path) {
	Set-Content -LiteralPath $Path -Encoding ascii -Value @'
param([string]$Ready, [string]$Output)
$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class TmuxSignalMatrixCtrlHandler {
	public delegate bool ConsoleCtrlDelegate(uint type);
	public static ConsoleCtrlDelegate Handler = new ConsoleCtrlDelegate(Ignore);
	public static bool Ignore(uint type) { return true; }
	[DllImport("kernel32.dll")]
	public static extern bool SetConsoleCtrlHandler(
	    ConsoleCtrlDelegate handler, bool add);
}
"@
Add-Type -TypeDefinition $source
[void][TmuxSignalMatrixCtrlHandler]::SetConsoleCtrlHandler(
    [TmuxSignalMatrixCtrlHandler]::Handler, $true)
[Console]::TreatControlCAsInput = $true
Set-Content -LiteralPath $Ready -Encoding ascii -Value "ready"
$deadline = [DateTime]::UtcNow.AddSeconds(20)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([Console]::KeyAvailable) {
		$key = [Console]::ReadKey($true)
		if ([int][char]$key.KeyChar -eq 3) {
			Set-Content -LiteralPath $Output -Encoding ascii `
			    -Value "ETX"
			exit 0
		}
	}
	Start-Sleep -Milliseconds 50
}
exit 2
'@
}

if ($Iterations -lt 1) {
	throw "-Iterations must be at least 1"
}

$serverName = "signal-matrix-" + [Guid]::NewGuid().ToString("N")
$temp = Join-Path ([System.IO.Path]::GetTempPath()) $serverName
New-Item -ItemType Directory -Force -Path $temp | Out-Null

try {
	for ($i = 1; $i -le $Iterations; $i++) {
		Write-Host ("[SIGNAL] iteration {0}/{1}" -f $i, $Iterations)
		Invoke-SignalTmux $serverName @(
		    "new-session", "-d", "-s", "signals", "cmd.exe") |
		    Out-Null
		Start-Sleep -Milliseconds 700

		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0",
		    "timeout /t 30 /nobreak", "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "timeout.exe"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-c") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0",
		    'powershell -NoProfile -Command "Start-Sleep -Seconds 30"',
		    "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "powershell.exe"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-c") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0",
		    "timeout /t 30 /nobreak", "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "timeout.exe"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-Break") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		$breakScript = Join-Path $temp "ctrl-break-$i.ps1"
		$breakReady = Join-Path $temp "ctrl-break-ready-$i.txt"
		$breakOutput = Join-Path $temp "ctrl-break-output-$i.txt"
		Write-CtrlBreakProbe $breakScript
		$breakCommand = "powershell -NoProfile -NonInteractive " +
		    "-ExecutionPolicy Bypass -File `"$breakScript`" " +
		    "-Ready `"$breakReady`" -Output `"$breakOutput`""
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", $breakCommand,
		    "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "powershell.exe"
		Wait-FileContains $breakReady "ready"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-Break") | Out-Null
		Wait-FileContains $breakOutput "CTRL_BREAK"
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0",
		    "choice /c yn /t 30 /d y", "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "choice.exe"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-c") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0",
		    "choice /c yn /t 30 /d y", "Enter") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "choice.exe"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-Break") | Out-Null
		Wait-CurrentCommand $serverName "signals:0.0" "cmd.exe"

		$rawScript = Join-Path $temp "raw-etx-$i.ps1"
		$rawReady = Join-Path $temp "raw-etx-ready-$i.txt"
		$rawOutput = Join-Path $temp "raw-etx-output-$i.txt"
		Write-RawEtXProbe $rawScript
		$rawCommand = "powershell -NoProfile -NonInteractive " +
		    "-ExecutionPolicy Bypass -File `"$rawScript`" " +
		    "-Ready `"$rawReady`" -Output `"$rawOutput`""
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", $rawCommand,
		    "Enter") | Out-Null
		Wait-FileContains $rawReady "ready"
		Invoke-SignalTmux $serverName @(
		    "send-keys", "-t", "signals:0.0", "C-c") | Out-Null
		Wait-FileContains $rawOutput "ETX"

		Invoke-SignalTmux $serverName @(
		    "kill-session", "-t", "signals") | Out-Null
		Write-Host ("[SIGNAL] iteration {0}/{1} passed" -f `
		    $i, $Iterations)
	}
} finally {
	try {
		Invoke-SignalTmux $serverName @("kill-server") 5 | Out-Null
	} catch {
	}
	if (Test-Path -LiteralPath $temp) {
		Remove-Item -LiteralPath $temp -Recurse -Force
	}
}

Write-Host ("Windows signal matrix stress passed: iterations={0}" -f `
    $Iterations)
