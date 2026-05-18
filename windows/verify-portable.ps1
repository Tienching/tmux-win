param(
	[string]$Tmux = "",
	[string]$ServerName = "",
	[int]$TimeoutSeconds = 20
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

if ([string]::IsNullOrWhiteSpace($ServerName)) {
	$ServerName = "verify-" + [Guid]::NewGuid().ToString("N")
}
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

function Invoke-VerifyTmux([string[]]$Arguments, [int]$Timeout = $TimeoutSeconds,
    [string]$Config = "NUL") {
	$allArguments = @("-L", $ServerName)
	if (-not [string]::IsNullOrEmpty($Config)) {
		$allArguments += @("-f", $Config)
	}
	$allArguments += $Arguments

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

function Wait-PaneContains([string]$Needle, [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$capture = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$capture = (Invoke-VerifyTmux @("capture-pane", "-p",
		    "-t", "verify:0.0")).Out
		if ($capture -like "*$Needle*") {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	throw "pane did not contain expected text: $Needle"
}

try {
	$version = (& $Tmux -V 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "tmux -V failed"
	}
	Write-Host "[PASS] version: $($version -join ' ')"

	Invoke-VerifyTmux @("new-session", "-d", "-s", "verify",
	    "cmd.exe") | Out-Null
	Write-Host "[PASS] detached session"

	Invoke-VerifyTmux @("send-keys", "-t", "verify:0.0",
	    "echo TMUX_WIN32_VERIFY_PORTABLE", "Enter") | Out-Null
	Wait-PaneContains "TMUX_WIN32_VERIFY_PORTABLE"
	Write-Host "[PASS] pane input/output"

	$sessions = (Invoke-VerifyTmux @("list-sessions")).Out
	if ($sessions -notlike "*verify*") {
		throw "verify session not listed"
	}
	Write-Host "[PASS] command client"

	Invoke-VerifyTmux @("kill-server") | Out-Null
	Write-Host "[PASS] kill-server"
	Write-Host "Windows portable tmux quick verification passed."
} finally {
	try {
		Invoke-VerifyTmux @("kill-server") 5 | Out-Null
	} catch {
	}
	if (Test-Path -LiteralPath $Endpoint) {
		Remove-Item -LiteralPath $Endpoint -Force
	}
}
