<#
.SYNOPSIS
  Verify tmux command clients remain responsive while the Windows clipboard
  is held open by another process.  After P1-11 the clipboard retry loop
  blocks for at most ~150 ms (3 retries x 50 ms), so a simple list-sessions
  should complete well under 500 ms even under contention.
#>
param(
	[string]$Tmux = "",
	[int]$HoldSeconds = 5,
	[int]$ResponseThresholdMs = 500,
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

$ServerName = "clip-resp-" + [Guid]::NewGuid().ToString("N")
$SessionName = "resptest"
$Temp = Join-Path ([System.IO.Path]::GetTempPath()) $ServerName
$LockerScript = Join-Path $Temp "clipboard-locker.ps1"

# -- P/Invoke for clipboard locking ------------------------------------------

$clipboardNativeSource = @"
using System;
using System.Runtime.InteropServices;
public static class ClipboardNative {
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool OpenClipboard(IntPtr hWndNewOwner);
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool CloseClipboard();
}
"@

# -- Helper: run a tmux command and measure elapsed time ----------------------

function Invoke-TmuxCommand([string[]]$Arguments) {
	$allArgs = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	# Build the argument string, quoting as needed
	$psi.Arguments = ($allArgs | ForEach-Object {
		if ($_ -match '[ \t"']') { "`"$( $_ -replace '"','\\"' )`"" } else { $_ }
	}) -join " "
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false

	$sw = [Diagnostics.Stopwatch]::StartNew()
	$process = [System.Diagnostics.Process]::Start($psi)
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	if (-not $process.WaitForExit(10000)) {
		try { $process.Kill() } catch {}
		throw "tmux timed out: $($Arguments -join ' ')"
	}
	$process.WaitForExit()
	$sw.Stop()

	$stdout = $stdoutTask.Result
	$stderr = $stderrTask.Result
	if ($process.ExitCode -ne 0) {
		throw "tmux failed: $($Arguments -join ' ')`nexit=$($process.ExitCode)`nstderr=$stderr"
	}

	return [pscustomobject]@{
		ElapsedMs = $sw.ElapsedMilliseconds
		Out = $stdout
		Err = $stderr
	}
}

# -- Write the locker script that holds the clipboard open -------------------

New-Item -ItemType Directory -Force -Path $Temp | Out-Null
Set-Content -LiteralPath $LockerScript -Encoding ascii -Value @'
param([string]$StartedFile, [int]$HoldSeconds)

$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class ClipLock {
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool OpenClipboard(IntPtr hWndNewOwner);
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool CloseClipboard();
}
"@
Add-Type -TypeDefinition $source

$deadline = [DateTime]::UtcNow.AddSeconds(5)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([ClipLock]::OpenClipboard([IntPtr]::Zero)) {
		try {
			Set-Content -LiteralPath $StartedFile -Encoding ascii -Value "locked"
			Start-Sleep -Seconds $HoldSeconds
		} finally {
			[void][ClipLock]::CloseClipboard()
		}
		exit 0
	}
	Start-Sleep -Milliseconds 25
}
Set-Content -LiteralPath $StartedFile -Encoding ascii -Value "failed"
exit 2
'@

# -- Main test logic ---------------------------------------------------------

Add-Type -TypeDefinition $clipboardNativeSource

$lockerProcess = $null
try {
	# 1. Start a tmux session
	Invoke-TmuxCommand @("new-session", "-d", "-s", $SessionName, "cmd.exe") |
	    Out-Null

	# 2. Hold the clipboard open in a background process
	$startedFile = Join-Path $Temp "locker-started.txt"
	$lockerProcess = Start-Process -FilePath powershell.exe `
	    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $LockerScript, "-StartedFile", $startedFile,
	    "-HoldSeconds", $HoldSeconds) `
	    -WindowStyle Hidden -PassThru

	# Wait for the locker to acquire the clipboard
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt 10000) {
		if (Test-Path -LiteralPath $startedFile) {
			$content = Get-Content -LiteralPath $startedFile -Raw
			if ($content -match "locked") { break }
			if ($content -match "failed") {
				throw "clipboard locker failed to acquire clipboard"
			}
		}
		Start-Sleep -Milliseconds 100
	}
	if ($sw.ElapsedMilliseconds -ge 10000) {
		throw "clipboard locker did not start in time"
	}

	# 3. Verify tmux commands still respond quickly while clipboard is held
	$maxMs = 0
	$probes = 5
	for ($i = 0; $i -lt $probes; $i++) {
		$result = Invoke-TmuxCommand @("list-sessions")
		if ($result.ElapsedMs -gt $maxMs) {
			$maxMs = $result.ElapsedMs
		}
		if ($i -lt $probes - 1) {
			Start-Sleep -Milliseconds 200
		}
	}

	if ($maxMs -gt $ResponseThresholdMs) {
		throw ("tmux response too slow under clipboard contention: " +
		    "max={0} ms threshold={1} ms" -f $maxMs, $ResponseThresholdMs)
	}

	Write-Host ("clipboard-responsiveness-stress passed: " +
	    "max_response={0} ms threshold={1} ms probes={2}" -f
	    $maxMs, $ResponseThresholdMs, $probes)
} finally {
	# 4. Clean up
	if ($null -ne $lockerProcess -and -not $lockerProcess.WaitForExit(15000)) {
		try { $lockerProcess.Kill() } catch {}
	}
	try {
		Invoke-TmuxCommand @("kill-server") | Out-Null
	} catch {}
	if (-not $KeepTemp -and (Test-Path -LiteralPath $Temp)) {
		Remove-Item -LiteralPath $Temp -Recurse -Force
	}
}
