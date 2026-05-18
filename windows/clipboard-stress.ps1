param(
	[string]$Tmux = "",
	[int]$Iterations = 3,
	[int]$HoldMilliseconds = 500,
	[int]$TimeoutSeconds = 60,
	[switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Iterations -lt 1) {
	throw "Iterations must be at least 1"
}
if ($HoldMilliseconds -lt 1) {
	throw "HoldMilliseconds must be at least 1"
}

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

$ServerName = "codex-clipboard-stress-" +
    [Guid]::NewGuid().ToString("N")
$SessionName = "clipstress"
$Temp = Join-Path ([System.IO.Path]::GetTempPath()) $ServerName
$LockerScript = Join-Path $Temp "clipboard-locker.ps1"
$AttachedProcess = $null
$AttachedOutputTask = $null
$AttachedErrorTask = $null

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

function Invoke-ClipboardStressTmux([string[]]$Arguments,
    [switch]$AllowFailure) {
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
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		try {
			$process.Kill()
		} catch {
		}
		throw "tmux timed out: $($Arguments -join ' ')"
	}
	$process.WaitForExit()
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Out = $stdoutTask.Result
		Err = $stderrTask.Result
	}
	if (-not $AllowFailure -and $result.ExitCode -ne 0) {
		throw @"
tmux failed: $($Arguments -join ' ')
exit code: $($result.ExitCode)
stdout:
$($result.Out)
stderr:
$($result.Err)
"@
	}
	return $result
}

function Invoke-ClipboardOperation([scriptblock]$Script) {
	$lastError = $null
	for ($i = 0; $i -lt 10; $i++) {
		try {
			return & $Script
		} catch {
			$lastError = $_
			Start-Sleep -Milliseconds 100
		}
	}
	throw $lastError
}

function Initialize-SystemClipboard {
	try {
		Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
		Invoke-ClipboardOperation {
			[System.Windows.Forms.Clipboard]::ContainsText() | Out-Null
		}
		return $true
	} catch {
		return $false
	}
}

function Save-SystemClipboard {
	return Invoke-ClipboardOperation {
		$containsText = [System.Windows.Forms.Clipboard]::ContainsText()
		$text = if ($containsText) {
			[System.Windows.Forms.Clipboard]::GetText()
		} else {
			$null
		}
		[pscustomobject]@{
			Data = [System.Windows.Forms.Clipboard]::GetDataObject()
			ContainsText = $containsText
			Text = $text
		}
	}
}

function Restore-SystemClipboard($Data) {
	$restored = $false
	if ($null -ne $Data -and $null -ne $Data.Data) {
		try {
			Invoke-ClipboardOperation {
				[System.Windows.Forms.Clipboard]::SetDataObject(
				    $Data.Data, $true)
			} | Out-Null
			$restored = $true
		} catch {
		}
	}
	if (-not $restored -and $null -ne $Data -and $Data.ContainsText) {
		try {
			Set-SystemClipboardText $Data.Text
			$restored = $true
		} catch {
		}
	}
	if (-not $restored) {
		try {
			Invoke-ClipboardOperation {
				[System.Windows.Forms.Clipboard]::Clear()
			} | Out-Null
		} catch {
		}
	}
}

function Set-SystemClipboardText([string]$Text) {
	Invoke-ClipboardOperation {
		if ([string]::IsNullOrEmpty($Text)) {
			[System.Windows.Forms.Clipboard]::Clear()
		} else {
			[System.Windows.Forms.Clipboard]::SetText($Text)
		}
	} | Out-Null
}

function Get-SystemClipboardText {
	return Invoke-ClipboardOperation {
		[System.Windows.Forms.Clipboard]::GetText()
	}
}

function Wait-SystemClipboardText([string]$Name, [string]$Needle,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$text = Get-SystemClipboardText
		if ($text -like "*$Needle*") {
			return $text
		}
		Start-Sleep -Milliseconds 100
	}
	throw "$Name did not update Windows clipboard"
}

function Wait-FileContains([string]$Name, [string]$Path, [string]$Needle,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			$content = Get-Content -LiteralPath $Path -Raw
			if ($content -like "*$Needle*") {
				return $content
			}
		}
		Start-Sleep -Milliseconds 100
	}
	throw "$Name did not contain expected text: $Needle"
}

function Wait-BufferContains([string]$Name, [string]$Needle,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$buffer = (Invoke-ClipboardStressTmux @("show-buffer")).Out
		if ($buffer -like "*$Needle*") {
			return $buffer
		}
		Start-Sleep -Milliseconds 100
	}
	throw "$Name did not update tmux buffer"
}

function Start-ClipboardLocker([string]$Name) {
	$started = Join-Path $Temp "$Name-started.txt"
	$process = Start-Process -FilePath powershell.exe `
	    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $LockerScript, "-StartedFile", $started,
	    "-HoldMilliseconds", $HoldMilliseconds) `
	    -WindowStyle Hidden -PassThru
	Wait-FileContains "clipboard locker" $started "locked" 10000 |
	    Out-Null
	return $process
}

function Wait-ClipboardLocker([System.Diagnostics.Process]$Process) {
	if ($null -eq $Process) {
		return
	}
	if (-not $Process.WaitForExit(15000)) {
		$Process.Kill()
		throw "clipboard locker did not exit"
	}
	if ($Process.ExitCode -ne 0) {
		throw "clipboard locker exited with $($Process.ExitCode)"
	}
}

function Start-AttachedClient {
	$allArguments = @("-L", $ServerName, "-f", "NUL", "attach",
	    "-t", $SessionName)
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardInput = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	$process = [System.Diagnostics.Process]::Start($psi)
	$script:AttachedOutputTask = $process.StandardOutput.ReadToEndAsync()
	$script:AttachedErrorTask = $process.StandardError.ReadToEndAsync()
	return $process
}

function Get-AttachedClientName {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt 10000) {
		$clients = (Invoke-ClipboardStressTmux @("list-clients",
		    "-F", "#{client_name}") -AllowFailure).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
		if (@($clients).Count -gt 0) {
			return @($clients)[0]
		}
		Start-Sleep -Milliseconds 100
	}
	throw "attached client did not appear"
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null
Set-Content -LiteralPath $LockerScript -Encoding ascii -Value @'
param(
	[string]$StartedFile,
	[int]$HoldMilliseconds
)

$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class ClipboardNative {
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool OpenClipboard(IntPtr hWndNewOwner);
	[DllImport("user32.dll", SetLastError=true)]
	public static extern bool CloseClipboard();
}
"@
Add-Type -TypeDefinition $source

$deadline = [DateTime]::UtcNow.AddSeconds(5)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([ClipboardNative]::OpenClipboard([IntPtr]::Zero)) {
		try {
			Set-Content -LiteralPath $StartedFile -Encoding ascii `
			    -Value "locked"
			Start-Sleep -Milliseconds $HoldMilliseconds
		} finally {
			[void][ClipboardNative]::CloseClipboard()
		}
		exit 0
	}
	Start-Sleep -Milliseconds 25
}
Set-Content -LiteralPath $StartedFile -Encoding ascii -Value "failed"
exit 2
'@

$savedClipboard = $null
try {
	if (-not (Initialize-SystemClipboard)) {
		Write-Host "[SKIP] Windows clipboard unavailable"
		exit 0
	}
	$savedClipboard = Save-SystemClipboard

	Invoke-ClipboardStressTmux @("new-session", "-d", "-s",
	    $SessionName, "cmd.exe") | Out-Null
	$AttachedProcess = Start-AttachedClient
	$clientName = Get-AttachedClientName

	for ($iteration = 0; $iteration -lt $Iterations; $iteration++) {
		$setText = "TMUX_WIN32_CLIP_STRESS_SET_${ServerName}_$iteration"
		$locker = Start-ClipboardLocker "set-$iteration"
		try {
			Invoke-ClipboardStressTmux @("set-buffer", "-w",
			    "-t", $clientName, "-b", "clip-stress",
			    $setText) | Out-Null
		} finally {
			Wait-ClipboardLocker $locker
		}
		Wait-SystemClipboardText "set-buffer -w retry" $setText `
		    10000 | Out-Null

		$getText = "TMUX_WIN32_CLIP_STRESS_GET_${ServerName}_$iteration"
		Set-SystemClipboardText $getText
		Wait-SystemClipboardText "prepare refresh-client -l" `
		    $getText 10000 | Out-Null
		$locker = Start-ClipboardLocker "get-$iteration"
		try {
			Invoke-ClipboardStressTmux @("refresh-client", "-l",
			    "-t", $clientName) | Out-Null
		} finally {
			Wait-ClipboardLocker $locker
		}
		Wait-BufferContains "refresh-client -l retry" $getText `
		    10000 | Out-Null
	}

	Write-Host ("Windows clipboard stress passed: iterations={0};hold_ms={1}" -f
	    $Iterations, $HoldMilliseconds)
} finally {
	if ($null -ne $savedClipboard) {
		Restore-SystemClipboard $savedClipboard
	}
	try {
		Invoke-ClipboardStressTmux @("detach-client", "-s",
		    $SessionName) -AllowFailure | Out-Null
	} catch {
	}
	if ($null -ne $AttachedProcess -and
	    -not $AttachedProcess.WaitForExit(5000)) {
		try {
			$AttachedProcess.Kill()
		} catch {
		}
	}
	try {
		Invoke-ClipboardStressTmux @("kill-server") -AllowFailure |
		    Out-Null
	} catch {
	}
	if (-not $KeepTemp -and (Test-Path -LiteralPath $Temp)) {
		Remove-Item -LiteralPath $Temp -Recurse -Force
	}
}
