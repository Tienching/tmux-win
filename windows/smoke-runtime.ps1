param(
	[string]$Tmux = "",
	[int]$TimeoutSeconds = 10,
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

$ServerName = "codex-smoke-" + [Guid]::NewGuid().ToString("N")
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

function Invoke-NamedTmux([string]$Name, [string[]]$Arguments,
    [int]$Timeout = $TimeoutSeconds, [string]$Config = "NUL") {
	$allArguments = @("-L", $Name)
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

function Invoke-SmokeTmux([string[]]$Arguments,
    [int]$Timeout = $TimeoutSeconds, [string]$Config = "NUL") {
	return Invoke-NamedTmux $ServerName $Arguments $Timeout $Config
}

function Start-SmokeTmuxProcess([string[]]$Arguments,
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

function Start-SmokePowerShellProcess([string[]]$Arguments) {
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = (Get-Command powershell.exe).Source
	$psi.Arguments = ($Arguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	return [System.Diagnostics.Process]::Start($psi)
}

function Assert-Contains([string]$Name, [string]$Text, [string]$Needle) {
	if ($Text -notlike "*$Needle*") {
		throw "$Name did not contain expected text: $Needle"
	}
}

function Assert-FileContains([string]$Name, [string]$Path, [string]$Needle) {
	if (-not (Test-Path -LiteralPath $Path)) {
		throw "$Name did not create file: $Path"
	}
	$content = Get-Content -LiteralPath $Path -Raw
	Assert-Contains $Name $content $Needle
}

function Wait-FileContains([string]$Name, [string]$Path, [string]$Needle,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if (Test-Path -LiteralPath $Path) {
			try {
				$content = Get-Content -LiteralPath $Path -Raw
				if ($content -like "*$Needle*") {
					return
				}
			} catch {
			}
		}
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains $Name $Path $Needle
}

function Normalize-SmokePathString([string]$Path) {
	$prefix = "Microsoft.PowerShell.Core\FileSystem::"
	$text = $Path.TrimEnd('\', '/')
	if ($text.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
		$text = $text.Substring($prefix.Length)
	}
	if ($text.StartsWith("\\?\UNC\", [StringComparison]::Ordinal)) {
		$text = "\" + $text.Substring(7)
	} elseif ($text.StartsWith("\\?\", [StringComparison]::Ordinal)) {
		$text = $text.Substring(4)
	}
	return $text.TrimEnd('\', '/')
}

function ConvertTo-SmokeExtendedPath([string]$Path) {
	$text = Normalize-SmokePathString $Path
	if ($text.StartsWith("\\", [StringComparison]::Ordinal)) {
		return "\\?\UNC\" + $text.Substring(2)
	}
	return "\\?\" + $text
}

function Resolve-SmokePath([string]$Path) {
	try {
		$resolved = (Resolve-Path -LiteralPath $Path).Path
		return Normalize-SmokePathString $resolved
	} catch {
		$extended = ConvertTo-SmokeExtendedPath $Path
		$resolved = (Resolve-Path -LiteralPath $extended).Path
		return Normalize-SmokePathString $resolved
	}
}

function Test-SmokePath([string]$Path) {
	$text = Normalize-SmokePathString $Path
	if ([System.IO.Directory]::Exists($text) -or
	    [System.IO.File]::Exists($text)) {
		return $true
	}
	$extended = ConvertTo-SmokeExtendedPath $text
	return ([System.IO.Directory]::Exists($extended) -or
	    [System.IO.File]::Exists($extended))
}

function New-LongSmokeDirectory([string]$Base, [string]$Name,
    [int]$TargetLength = 230) {
	$path = Join-Path $Base $Name
	while ($path.Length -lt $TargetLength) {
		$path = Join-Path $path ("segment-{0:D3}" -f $path.Length)
	}
	$extended = ConvertTo-SmokeExtendedPath $path
	[System.IO.Directory]::CreateDirectory($extended) | Out-Null
	if (-not [System.IO.Directory]::Exists($extended)) {
		throw "failed to create long path: $path"
	}
	return [pscustomobject]@{
		Path = $path
		ExtendedPath = $extended
	}
}

function Get-DescendantProcessIds([int]$RootId) {
	$result = [System.Collections.Generic.List[int]]::new()
	$queue = [System.Collections.Generic.Queue[int]]::new()
	$queue.Enqueue($RootId)
	while ($queue.Count -gt 0) {
		$id = $queue.Dequeue()
		$children = @(Get-CimInstance Win32_Process `
		    -Filter "ParentProcessId=$id" -ErrorAction SilentlyContinue)
		foreach ($child in $children) {
			$childId = [int]$child.ProcessId
			$result.Add($childId)
			$queue.Enqueue($childId)
		}
	}
	return @($result.ToArray())
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
	$lastError = $null

	if ($null -ne $Data -and $null -ne $Data.Data) {
		try {
			Invoke-ClipboardOperation {
				[System.Windows.Forms.Clipboard]::SetDataObject($Data.Data,
				    $true)
			} | Out-Null
			$restored = $true
		} catch {
			$lastError = $_
		}
	}
	if (-not $restored -and $null -ne $Data -and $Data.ContainsText) {
		try {
			Set-SystemClipboardText $Data.Text
			$restored = $true
		} catch {
			$lastError = $_
		}
	}
	if (-not $restored) {
		try {
			Invoke-ClipboardOperation {
				[System.Windows.Forms.Clipboard]::Clear()
			} | Out-Null
			$restored = $true
		} catch {
			$lastError = $_
		}
	}
	if (-not $restored) {
		$message = if ($null -ne $lastError) {
			$lastError.Exception.Message
		} else {
			"unknown error"
		}
		Write-Host "[WARN] failed to restore Windows clipboard: $message"
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
    [int]$Timeout = 5000) {
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

function Write-Pass([string]$Name) {
	Write-Host "[PASS] $Name"
}

$ControlProcess = $null
$ControlLines = $null
$ControlReadTask = $null
$AttachedProcess = $null
$AttachedOutputTask = $null
$AttachedErrorTask = $null
$MenuProcess = $null
$OldParseDir = [Environment]::GetEnvironmentVariable(
    "TMUX_WIN32_PARSE_DIR", "Process")

function Start-ControlClient {
	$allArguments = @("-L", $ServerName, "-f", "NUL", "-C", "attach",
	    "-t", "smoke")
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardInput = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false

	$script:ControlProcess = [System.Diagnostics.Process]::Start($psi)
	$script:ControlLines = [System.Collections.Generic.List[string]]::new()
	$script:ControlReadTask =
	    $script:ControlProcess.StandardOutput.ReadLineAsync()
}

function Stop-ControlClient {
	if ($script:ControlProcess -ne $null -and
	    -not $script:ControlProcess.HasExited) {
		try {
			$script:ControlProcess.Kill()
		} catch {
		}
	}
}

function Send-ControlCommand([string]$Command) {
	$script:ControlProcess.StandardInput.WriteLine($Command)
	$script:ControlProcess.StandardInput.Flush()
}

function Read-ControlUntil([string]$Name, [string]$Needle,
    [int]$Timeout = 6000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if ($script:ControlReadTask.Wait(100)) {
			$line = $script:ControlReadTask.Result
			if ($null -eq $line) {
				break
			}
			$script:ControlLines.Add($line)
			$script:ControlReadTask =
			    $script:ControlProcess.StandardOutput.ReadLineAsync()
			$text = $script:ControlLines -join "`n"
			if ($text -like "*$Needle*") {
				return $text
			}
		}
		if ($script:ControlProcess.HasExited) {
			break
		}
	}

	$text = if ($script:ControlLines -ne $null) {
		$script:ControlLines -join "`n"
	} else {
		""
	}
	throw "$Name did not produce expected control output: $Needle; output: $text"
}

function Drain-ControlOutput([int]$Quiet = 700, [int]$Timeout = 8000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$quietSw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		if ($script:ControlReadTask.Wait(100)) {
			$line = $script:ControlReadTask.Result
			if ($null -eq $line) {
				break
			}
			$script:ControlLines.Add($line)
			$script:ControlReadTask =
			    $script:ControlProcess.StandardOutput.ReadLineAsync()
			$quietSw.Restart()
			continue
		}
		if ($script:ControlProcess.HasExited) {
			break
		}
		if ($quietSw.ElapsedMilliseconds -ge $Quiet) {
			break
		}
	}
}

function Wait-PaneCurrentCommand([string]$Name, [string]$Target,
    [string]$Needle, [int]$Timeout = 8000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$command = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$command = (Invoke-SmokeTmux @("display-message", "-p",
		    "-t", $Target, "#{pane_current_command}")).Out.Trim()
		if ($command -like "*$Needle*") {
			return $command
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected text: $Needle; last: $command"
}

function Wait-WindowGone([string]$Name, [string]$Target,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		try {
			Invoke-SmokeTmux @("display-message", "-p", "-t",
			    $Target, "#{window_id}") 3 | Out-Null
		} catch {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not close: $Target"
}

function Wait-PaneDead([string]$Name, [string]$Target,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$dead = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$dead = (Invoke-SmokeTmux @("display-message", "-p", "-t",
		    $Target, "#{pane_dead}") 3).Out.Trim()
		if ($dead -eq "1") {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not reach dead pane state: $dead"
}

function Wait-PaneContains([string]$Name, [string]$Target, [string]$Needle,
    [int]$Timeout = 10000) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$capture = ""
	while ($sw.ElapsedMilliseconds -lt $Timeout) {
		$capture = (Invoke-SmokeTmux @("capture-pane", "-p",
		    "-t", $Target)).Out
		if ($capture -like "*$Needle*") {
			return $capture
		}
		Start-Sleep -Milliseconds 200
	}
	throw "$Name did not contain expected text: $Needle"
}

function Test-WindowExists([string]$Target) {
	try {
		Invoke-SmokeTmux @("display-message", "-p", "-t", $Target,
		    "#{window_id}") 3 | Out-Null
		return $true
	} catch {
		return $false
	}
}

function Close-WindowGracefully([string]$Name, [string]$Target) {
	if (Test-WindowExists $Target) {
		try {
			$panes = (Invoke-SmokeTmux @("list-panes", "-t",
			    $Target, "-F", "#{pane_index}") 5).Out
			foreach ($pane in $panes -split "`r?`n") {
				if ([string]::IsNullOrWhiteSpace($pane)) {
					continue
				}
				Invoke-SmokeTmux @("send-keys", "-t",
				    "$Target.$pane", "exit", "Enter") 5 |
				    Out-Null
			}
		} catch {
		}
		try {
			Wait-WindowGone $Name $Target
			return
		} catch {
		}
	}
	if (Test-WindowExists $Target) {
		try {
			Invoke-SmokeTmux @("kill-window", "-t", $Target) 60 |
			    Out-Null
		} catch {
			return
		}
	}
}

function Start-AttachedClient([string]$Session) {
	$allArguments = @("-L", $ServerName, "-f", "NUL", "attach",
	    "-t", $Session)
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardInput = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false

	$script:AttachedProcess = [System.Diagnostics.Process]::Start($psi)
	$script:AttachedOutputTask =
	    $script:AttachedProcess.StandardOutput.ReadToEndAsync()
	$script:AttachedErrorTask =
	    $script:AttachedProcess.StandardError.ReadToEndAsync()
}

function Stop-AttachedClient {
	if ($script:AttachedProcess -ne $null -and
	    -not $script:AttachedProcess.HasExited) {
		try {
			$script:AttachedProcess.Kill()
		} catch {
		}
	}
}

function Stop-MenuProcess {
	if ($script:MenuProcess -ne $null -and
	    -not $script:MenuProcess.HasExited) {
		try {
			$script:MenuProcess.Kill()
		} catch {
		}
	}
}

function Get-SmokeTmuxProcesses {
	$escaped = $ServerName.Replace("'", "''")
	return @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object { $_.CommandLine -like "*$escaped*" })
}

function Stop-SmokeProcesses {
	Get-SmokeTmuxProcesses | ForEach-Object {
		try {
			Stop-Process -Id $_.ProcessId -Force
		} catch {
		}
	}
}

function Stop-SmokeServer([int]$Timeout = 60000) {
	$process = Start-SmokeTmuxProcess @("kill-server")
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
		$remaining = @(Get-SmokeTmuxProcesses)
		if ($remaining.Count -eq 0) {
			return
		}
		Start-Sleep -Milliseconds 200
	}
	Stop-SmokeProcesses
	$wait.Restart()
	while ($wait.ElapsedMilliseconds -lt 5000) {
		$remaining = @(Get-SmokeTmuxProcesses)
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

function Remove-SmokeTemp {
	if ($KeepTemp -or -not (Test-Path -LiteralPath $Temp)) {
		return
	}
	$lastError = $null
	for ($i = 0; $i -lt 20; $i++) {
		try {
			Remove-Item -LiteralPath $Temp -Recurse -Force
			return
		} catch {
			$lastError = $_
			try {
				Remove-Item -LiteralPath `
				    (ConvertTo-SmokeExtendedPath $Temp) `
				    -Recurse -Force
				return
			} catch {
				$lastError = $_
			}
			Start-Sleep -Milliseconds 500
		}
	}
	Write-Host ("[WARN] failed to remove smoke temp {0}: {1}" -f `
	    $Temp, $lastError.Exception.Message)
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null

try {
	$startupConfig = Join-Path $Temp "startup.conf"
	$sourceConfig = Join-Path $Temp "source.conf"
	$parseConfigDir = Join-Path $Temp "parse-configs"
	New-Item -ItemType Directory -Force -Path $parseConfigDir | Out-Null
	Set-Content -LiteralPath $startupConfig -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_STARTUP_CONFIG yes"
	Set-Content -LiteralPath $sourceConfig -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_SOURCE_FILE yes"
	Set-Content -LiteralPath (Join-Path $parseConfigDir "parse-a.conf") `
	    -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_PARSE_A yes"
	Set-Content -LiteralPath (Join-Path $parseConfigDir "parse-b.conf") `
	    -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_PARSE_B yes"
	Set-Item -Path "env:TMUX_WIN32_PARSE_DIR" -Value $parseConfigDir

	$defaultName = $ServerName + "-default-config"
	$defaultRoot = Join-Path $Temp "default-config"
	$defaultProgramData = Join-Path $defaultRoot "ProgramData"
	$defaultAppData = Join-Path $defaultRoot "AppData"
	$defaultHome = Join-Path $defaultRoot "Home"
	$defaultLocalAppData = Join-Path $defaultRoot "LocalAppData"
	New-Item -ItemType Directory -Force -Path `
	    (Join-Path $defaultProgramData "tmux"), `
	    (Join-Path $defaultAppData "tmux"), `
	    $defaultHome, $defaultLocalAppData | Out-Null
	Set-Content -LiteralPath `
	    (Join-Path $defaultProgramData "tmux\tmux.conf") `
	    -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_PROGRAMDATA_CONFIG yes"
	Set-Content -LiteralPath (Join-Path $defaultAppData "tmux\tmux.conf") `
	    -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_APPDATA_CONFIG yes"
	Set-Content -LiteralPath (Join-Path $defaultHome ".tmux.conf") `
	    -Encoding ascii -Value `
	    "set-environment -g TMUX_WIN32_HOME_CONFIG yes"

	$oldEnvironment = @{
		PROGRAMDATA = $env:PROGRAMDATA
		APPDATA = $env:APPDATA
		USERPROFILE = $env:USERPROFILE
		LOCALAPPDATA = $env:LOCALAPPDATA
	}
	try {
		$env:PROGRAMDATA = $defaultProgramData
		$env:APPDATA = $defaultAppData
		$env:USERPROFILE = $defaultHome
		$env:LOCALAPPDATA = $defaultLocalAppData

		Invoke-NamedTmux $defaultName @("new-session", "-d", "-s",
		    "defaultcfg", "cmd.exe") $TimeoutSeconds "" | Out-Null
		$defaultPollSw = [Diagnostics.Stopwatch]::StartNew()
		while ($defaultPollSw.ElapsedMilliseconds -lt 7000) {
			$programEnv = (Invoke-NamedTmux $defaultName `
			    @("show-environment", "-g",
			    "TMUX_WIN32_PROGRAMDATA_CONFIG") $TimeoutSeconds "").Out
			if ($programEnv -like "*TMUX_WIN32_PROGRAMDATA_CONFIG=yes*") { break }
			Start-Sleep -Milliseconds 100
		}
		$programEnv = (Invoke-NamedTmux $defaultName `
		    @("show-environment", "-g",
		    "TMUX_WIN32_PROGRAMDATA_CONFIG") $TimeoutSeconds "").Out
		$appEnv = (Invoke-NamedTmux $defaultName `
		    @("show-environment", "-g",
		    "TMUX_WIN32_APPDATA_CONFIG") $TimeoutSeconds "").Out
		$homeEnv = (Invoke-NamedTmux $defaultName `
		    @("show-environment", "-g",
		    "TMUX_WIN32_HOME_CONFIG") $TimeoutSeconds "").Out
		Assert-Contains "default ProgramData config" $programEnv `
		    "TMUX_WIN32_PROGRAMDATA_CONFIG=yes"
		Assert-Contains "default AppData config" $appEnv `
		    "TMUX_WIN32_APPDATA_CONFIG=yes"
		Assert-Contains "default home config" $homeEnv `
		    "TMUX_WIN32_HOME_CONFIG=yes"
		Invoke-NamedTmux $defaultName @("kill-server") `
		    $TimeoutSeconds "" | Out-Null
		Write-Pass "default config search"
	} finally {
		try {
			Invoke-NamedTmux $defaultName @("kill-server") 3 "" |
			    Out-Null
		} catch {
		}
		foreach ($key in $oldEnvironment.Keys) {
			if ($null -eq $oldEnvironment[$key]) {
				Remove-Item -Path "env:$key" -ErrorAction SilentlyContinue
			} else {
				Set-Item -Path "env:$key" -Value $oldEnvironment[$key]
			}
		}
	}

	$version = (Invoke-SmokeTmux @("-V")).Out.Trim()
	Assert-Contains "version" $version "tmux"
	Write-Pass "version: $version"

	$staleName = $ServerName + "-stale-endpoint"
	$staleEndpointDir = Join-Path $env:LOCALAPPDATA "tmux"
	$staleEndpoint = Join-Path $staleEndpointDir `
	    ($staleName + ".endpoint")
	New-Item -ItemType Directory -Force -Path $staleEndpointDir |
	    Out-Null
	Set-Content -LiteralPath $staleEndpoint -Encoding ascii `
	    -Value "not a valid tmux endpoint"
	try {
		Invoke-NamedTmux $staleName @("new-session", "-d", "-s",
		    "stalecheck", "cmd.exe") $TimeoutSeconds "NUL" |
		    Out-Null
		$stalePollSw = [Diagnostics.Stopwatch]::StartNew()
		while ($stalePollSw.ElapsedMilliseconds -lt 7000) {
			$staleSessions = (Invoke-NamedTmux $staleName `
			    @("list-sessions") $TimeoutSeconds "NUL").Out
			if ($staleSessions -like "*stalecheck:*") { break }
			Start-Sleep -Milliseconds 100
		}
		$staleSessions = (Invoke-NamedTmux $staleName `
		    @("list-sessions") $TimeoutSeconds "NUL").Out
		Assert-Contains "stale endpoint startup" $staleSessions `
		    "stalecheck:"
		Invoke-NamedTmux $staleName @("kill-server") `
		    $TimeoutSeconds "NUL" | Out-Null
	} finally {
		try {
			Invoke-NamedTmux $staleName @("kill-server") 3 "NUL" |
			    Out-Null
		} catch {
		}
		if (Test-Path -LiteralPath $staleEndpoint) {
			Remove-Item -LiteralPath $staleEndpoint -Force
		}
	}
	Write-Pass "stale endpoint startup"

	Invoke-SmokeTmux @("new-session", "-d", "-s", "smoke", "cmd.exe") `
	    $TimeoutSeconds $startupConfig | Out-Null
	$lifecyclePollSw = [Diagnostics.Stopwatch]::StartNew()
	while ($lifecyclePollSw.ElapsedMilliseconds -lt 7000) {
		$listSessions = (Invoke-SmokeTmux @("list-sessions")).Out
		if ($listSessions -like "*smoke:*") { break }
		Start-Sleep -Milliseconds 100
	}
	$listSessions = (Invoke-SmokeTmux @("list-sessions")).Out
	Assert-Contains "list-sessions" $listSessions "smoke:"
	Write-Pass "detached server lifecycle"

	$startupEnv = (Invoke-SmokeTmux @("show-environment", "-g",
	    "TMUX_WIN32_STARTUP_CONFIG")).Out
	Assert-Contains "startup config" $startupEnv `
	    "TMUX_WIN32_STARTUP_CONFIG=yes"
	Write-Pass "startup config"

	Invoke-SmokeTmux @("source-file", $sourceConfig) | Out-Null
	$sourceEnv = (Invoke-SmokeTmux @("show-environment", "-g",
	    "TMUX_WIN32_SOURCE_FILE")).Out
	Assert-Contains "source-file" $sourceEnv "TMUX_WIN32_SOURCE_FILE=yes"
	Write-Pass "source-file"

	Invoke-SmokeTmux @("source-file",
	    '%TMUX_WIN32_PARSE_DIR%\parse-*.conf') | Out-Null
	$parseEnvA = (Invoke-SmokeTmux @("show-environment", "-g",
	    "TMUX_WIN32_PARSE_A")).Out
	$parseEnvB = (Invoke-SmokeTmux @("show-environment", "-g",
	    "TMUX_WIN32_PARSE_B")).Out
	Assert-Contains "source-file Windows env glob" $parseEnvA `
	    "TMUX_WIN32_PARSE_A=yes"
	Assert-Contains "source-file Windows env glob" $parseEnvB `
	    "TMUX_WIN32_PARSE_B=yes"
	Write-Pass "source-file Windows env glob"

	$hookFile = Join-Path $Temp "after-new-window-hook.txt"
	$hookTarget = $hookFile.Replace('\', '/')
	$hookCommand = "run-shell `"cmd /c echo TMUX_WIN32_HOOK>$hookTarget`""
	Invoke-SmokeTmux @("set-hook", "-g", "after-new-window",
	    $hookCommand) | Out-Null
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "hookwin", "cmd.exe") | Out-Null
	$hookWait = [Diagnostics.Stopwatch]::StartNew()
	while ($hookWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $hookFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "after-new-window hook" $hookFile `
	    "TMUX_WIN32_HOOK"
	Close-WindowGracefully "hook cleanup" "smoke:hookwin"
	Invoke-SmokeTmux @("set-hook", "-gu", "after-new-window") | Out-Null
	Write-Pass "hooks"

	$loadBufferFile = Join-Path $Temp "load-buffer.txt"
	$saveBufferFile = Join-Path $Temp "save-buffer.txt"
	Set-Content -LiteralPath $loadBufferFile -Encoding ascii -NoNewline `
	    -Value "TMUX_WIN32_FILE_TRANSFER"
	Invoke-SmokeTmux @("load-buffer", "-b", "winfile",
	    $loadBufferFile) | Out-Null
	$loadedBuffer = (Invoke-SmokeTmux @("show-buffer", "-b",
	    "winfile")).Out
	Assert-Contains "load-buffer" $loadedBuffer `
	    "TMUX_WIN32_FILE_TRANSFER"
	Invoke-SmokeTmux @("save-buffer", "-b", "winfile",
	    $saveBufferFile) | Out-Null
	Assert-FileContains "save-buffer file" $saveBufferFile `
	    "TMUX_WIN32_FILE_TRANSFER"
	$stdoutBuffer = (Invoke-SmokeTmux @("save-buffer", "-b",
	    "winfile", "-")).Out
	Assert-Contains "save-buffer stdout" $stdoutBuffer `
	    "TMUX_WIN32_FILE_TRANSFER"
	Write-Pass "load/save-buffer file transfer"

	$largeBufferFile = Join-Path $Temp "load-buffer-large.bin"
	$largeSaveBufferFile = Join-Path $Temp "save-buffer-large.bin"
	$largeBytes = New-Object byte[] 65536
	for ($i = 0; $i -lt $largeBytes.Length; $i++) {
		$largeBytes[$i] = [byte]($i % 251)
	}
	[IO.File]::WriteAllBytes($largeBufferFile, $largeBytes)
	Invoke-SmokeTmux @("load-buffer", "-b", "winlarge",
	    $largeBufferFile) | Out-Null
	Invoke-SmokeTmux @("save-buffer", "-b", "winlarge",
	    $largeSaveBufferFile) | Out-Null
	$largeBufferHash = (Get-FileHash -LiteralPath $largeBufferFile `
	    -Algorithm SHA256).Hash
	$largeSaveBufferHash = (Get-FileHash -LiteralPath `
	    $largeSaveBufferFile -Algorithm SHA256).Hash
	if ($largeBufferHash -ne $largeSaveBufferHash) {
		throw "large binary buffer hash mismatch"
	}
	Write-Pass "load/save-buffer large binary"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_PANE_SMOKE", "Enter") | Out-Null
	$capture = Wait-PaneContains "pane I/O" "smoke:0.0" `
	    "TMUX_WIN32_PANE_SMOKE" 8000
	Assert-Contains "pane I/O" $capture "TMUX_WIN32_PANE_SMOKE"
	Write-Pass "pane send/capture"

	Invoke-SmokeTmux @("split-window", "-h", "-t", "smoke:0.0") |
	    Out-Null
	Start-Sleep -Milliseconds 500
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_LEFT", "Enter") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.1",
	    "echo TMUX_WIN32_RIGHT", "Enter") | Out-Null
	$left = Wait-PaneContains "left pane" "smoke:0.0" `
	    "TMUX_WIN32_LEFT" 8000
	$right = Wait-PaneContains "right pane" "smoke:0.1" `
	    "TMUX_WIN32_RIGHT" 8000
	Assert-Contains "left pane" $left "TMUX_WIN32_LEFT"
	Assert-Contains "right pane" $right "TMUX_WIN32_RIGHT"
	Write-Pass "split-window independent panes"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "swap", "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("split-window", "-h", "-t", "smoke:swap.0") |
	    Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:swap.0",
	    "echo TMUX_WIN32_SWAP_LEFT", "Enter") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:swap.1",
	    "echo TMUX_WIN32_SWAP_RIGHT", "Enter") | Out-Null
	Wait-PaneContains "swap left pane" "smoke:swap.0" `
	    "TMUX_WIN32_SWAP_LEFT" 8000 | Out-Null
	Wait-PaneContains "swap right pane" "smoke:swap.1" `
	    "TMUX_WIN32_SWAP_RIGHT" 8000 | Out-Null
	Invoke-SmokeTmux @("select-layout", "-t", "smoke:swap",
	    "even-horizontal") | Out-Null
	Invoke-SmokeTmux @("swap-pane", "-s", "smoke:swap.0", "-t",
	    "smoke:swap.1") | Out-Null
	$swapLeft = Wait-PaneContains "swap-pane left" "smoke:swap.0" `
	    "TMUX_WIN32_SWAP_RIGHT" 6000
	$swapRight = Wait-PaneContains "swap-pane right" "smoke:swap.1" `
	    "TMUX_WIN32_SWAP_LEFT" 6000
	Assert-Contains "swap-pane left" $swapLeft "TMUX_WIN32_SWAP_RIGHT"
	Assert-Contains "swap-pane right" $swapRight "TMUX_WIN32_SWAP_LEFT"
	Close-WindowGracefully "swap cleanup" "smoke:swap"
	Write-Pass "swap-pane layout"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "linkwin", "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:linkwin.0",
	    "echo TMUX_WIN32_LINK_WINDOW", "Enter") | Out-Null
	Wait-PaneContains "link-window pane" "smoke:linkwin.0" `
	    "TMUX_WIN32_LINK_WINDOW" 8000 | Out-Null
	Invoke-SmokeTmux @("new-session", "-d", "-s", "linkdst",
	    "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("link-window", "-s", "smoke:linkwin",
	    "-t", "linkdst:") | Out-Null
	$linkedWindows = (Invoke-SmokeTmux @("list-windows", "-t",
	    "linkdst", "-F", "#{window_name}")).Out
	Assert-Contains "link-window" $linkedWindows "linkwin"
	$linkedCapture = (Invoke-SmokeTmux @("capture-pane", "-p",
	    "-t", "linkdst:linkwin.0")).Out
	Assert-Contains "linked window pane" $linkedCapture `
	    "TMUX_WIN32_LINK_WINDOW"
	Invoke-SmokeTmux @("unlink-window", "-t", "linkdst:linkwin") |
	    Out-Null
	Close-WindowGracefully "linkdst cleanup" "linkdst:0"
	Close-WindowGracefully "link-window cleanup" "smoke:linkwin"
	Write-Pass "link/unlink window"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "reflow", "cmd.exe") | Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("split-window", "-h", "-t",
	    "smoke:reflow.0") | Out-Null
	Start-Sleep -Milliseconds 600
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:reflow.1",
	    "echo TMUX_WIN32_BREAK_JOIN", "Enter") | Out-Null
	Wait-PaneContains "break-join pane" "smoke:reflow.1" `
	    "TMUX_WIN32_BREAK_JOIN" 8000 | Out-Null
	$breakPaneId = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:reflow.1", "#{pane_id}")).Out.Trim()
	Invoke-SmokeTmux @("break-pane", "-d", "-n", "broken", "-s",
	    $breakPaneId) | Out-Null
	$brokenCapture = Wait-PaneContains "break-pane output" `
	    "smoke:broken.0" "TMUX_WIN32_BREAK_JOIN" 7000
	Assert-Contains "break-pane output" $brokenCapture `
	    "TMUX_WIN32_BREAK_JOIN"
	Invoke-SmokeTmux @("join-pane", "-h", "-s", "smoke:broken.0",
	    "-t", "smoke:reflow.0") | Out-Null
	$joinPollSw = [Diagnostics.Stopwatch]::StartNew()
	$reflowPanes = ""
	while ($joinPollSw.ElapsedMilliseconds -lt 7000) {
		$reflowPanes = (Invoke-SmokeTmux @("list-panes", "-t",
		    "smoke:reflow", "-F", "#{pane_index}")).Out
		if (($reflowPanes -split "`r?`n" | Where-Object {
		    $_ -ne "" }).Count -eq 2) { break }
		Start-Sleep -Milliseconds 100
	}
	$reflowPanes = (Invoke-SmokeTmux @("list-panes", "-t",
	    "smoke:reflow", "-F", "#{pane_index}")).Out
	if (@($reflowPanes -split "`r?`n" | Where-Object {
	    $_ -ne ""
	}).Count -ne 2) {
		throw "join-pane did not restore two panes: $reflowPanes"
	}
	Close-WindowGracefully "reflow cleanup" "smoke:reflow"
	Write-Pass "break/join pane"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "respawnp", "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "respawn pane window" "smoke:respawnp.0" `
	    "cmd.exe" 7000 | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:respawnp.0",
	    "echo TMUX_WIN32_RESPAWN_PANE_INITIAL", "Enter") | Out-Null
	$respawnCapture = Wait-PaneContains "respawn initial pane" `
	    "smoke:respawnp.0" "TMUX_WIN32_RESPAWN_PANE_INITIAL"
	Assert-Contains "respawn initial pane" $respawnCapture `
	    "TMUX_WIN32_RESPAWN_PANE_INITIAL"
	Invoke-SmokeTmux @("respawn-pane", "-k", "-t",
	    "smoke:respawnp.0", "cmd.exe") 60 | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:respawnp.0",
	    "echo TMUX_WIN32_RESPAWN_PANE", "Enter") | Out-Null
	$respawnCapture = Wait-PaneContains "respawn pane" `
	    "smoke:respawnp.0" "TMUX_WIN32_RESPAWN_PANE"
	Assert-Contains "respawn pane" $respawnCapture `
	    "TMUX_WIN32_RESPAWN_PANE"
	Close-WindowGracefully "respawn pane cleanup" "smoke:respawnp"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "respawnw", "cmd.exe") | Out-Null
	Invoke-SmokeTmux @("set-option", "-w", "-t", "smoke:respawnw",
	    "remain-on-exit", "on") | Out-Null
	Wait-PaneCurrentCommand "respawn window window" "smoke:respawnw.0" `
	    "cmd.exe" 7000 | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:respawnw.0",
	    "echo TMUX_WIN32_RESPAWN_WINDOW_INITIAL & exit", "Enter") |
	    Out-Null
	Wait-PaneDead "respawn window dead pane" "smoke:respawnw.0"
	$respawnCapture = (Invoke-SmokeTmux @("capture-pane", "-p", "-t",
	    "smoke:respawnw.0")).Out
	Assert-Contains "respawn initial window" $respawnCapture `
	    "TMUX_WIN32_RESPAWN_WINDOW_INITIAL"
	Invoke-SmokeTmux @("respawn-window", "-t", "smoke:respawnw",
	    "cmd.exe") 60 | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:respawnw.0",
	    "echo TMUX_WIN32_RESPAWN_WINDOW", "Enter") | Out-Null
	$respawnCapture = Wait-PaneContains "respawn window" `
	    "smoke:respawnw.0" "TMUX_WIN32_RESPAWN_WINDOW"
	Assert-Contains "respawn window" $respawnCapture `
	    "TMUX_WIN32_RESPAWN_WINDOW"
	Invoke-SmokeTmux @("set-option", "-w", "-t", "smoke:respawnw",
	    "remain-on-exit", "off") | Out-Null
	Close-WindowGracefully "respawn window cleanup" "smoke:respawnw"
	Write-Pass "respawn pane/window"

	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "killtree", "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "killtree window" "smoke:killtree.0" `
	    "cmd.exe" 7000 | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:killtree.0",
	    "timeout /t 30 /nobreak", "Enter") | Out-Null
	Wait-PaneCurrentCommand "kill-pane active child command" `
	    "smoke:killtree.0" "timeout.exe" | Out-Null
	$killTreePid = [int](Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:killtree.0", "#{pane_pid}")).Out.Trim()
	$killTreeChildren = @(Get-DescendantProcessIds $killTreePid)
	if ($killTreeChildren.Count -eq 0) {
		throw "kill-pane process tree had no child processes"
	}
	$killPaneProcess = Start-SmokeTmuxProcess @("kill-pane", "-t",
	    "smoke:killtree.0")
	if ($killPaneProcess.WaitForExit(60000)) {
		if ($killPaneProcess.ExitCode -ne 0) {
			$stderr = $killPaneProcess.StandardError.ReadToEnd()
			$stdout = $killPaneProcess.StandardOutput.ReadToEnd()
			throw ("kill-pane exited with {0}: {1} {2}" -f `
			    $killPaneProcess.ExitCode, $stdout, $stderr)
		}
	} else {
		try {
			$killPaneProcess.Kill()
		} catch {
		}
	}
	$killPaneWaitSw = [Diagnostics.Stopwatch]::StartNew()
	while ($killPaneWaitSw.ElapsedMilliseconds -lt 12000) {
		$aliveKillTreeChildren = @($killTreeChildren | Where-Object {
		    Get-Process -Id $_ -ErrorAction SilentlyContinue
		})
		if ($aliveKillTreeChildren.Count -eq 0) { break }
		Start-Sleep -Milliseconds 100
	}
	$aliveKillTreeChildren = @($killTreeChildren | Where-Object {
	    Get-Process -Id $_ -ErrorAction SilentlyContinue
	})
	if ($aliveKillTreeChildren.Count -ne 0) {
		throw ("kill-pane left child processes: " +
		    ($aliveKillTreeChildren -join ","))
	}
	Write-Pass "kill-pane process tree"

	Invoke-SmokeTmux @("resize-pane", "-x", "50", "-t", "smoke:0.0") |
	    Out-Null
	$resizePollSw = [Diagnostics.Stopwatch]::StartNew()
	$panes = ""
	while ($resizePollSw.ElapsedMilliseconds -lt 5000) {
		$panes = (Invoke-SmokeTmux @("list-panes", "-F",
		    "#{pane_index}:#{pane_width}x#{pane_height}:#{pane_dead}:#{pane_current_command}",
		    "-t", "smoke:0")).Out
		if ($panes -like "*0:50x*") { break }
		Start-Sleep -Milliseconds 100
	}
	$panes = (Invoke-SmokeTmux @("list-panes", "-F",
	    "#{pane_index}:#{pane_width}x#{pane_height}:#{pane_dead}:#{pane_current_command}",
	    "-t", "smoke:0")).Out
	Assert-Contains "resize-pane" $panes "0:50x"
	Assert-Contains "resize-pane" $panes ":0:"
	Assert-Contains "pane current command" $panes "cmd.exe"
	Write-Pass "resize-pane"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "timeout /t 30 /nobreak", "Enter") | Out-Null
	Wait-PaneCurrentCommand "pane current command active child" `
	    "smoke:0.0" "timeout.exe" | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0", "C-c") | Out-Null
	$interruptedCommand = Wait-PaneCurrentCommand "pane C-c interrupt" `
	    "smoke:0.0" "cmd.exe" 14000
	Assert-Contains "pane C-c interrupt" $interruptedCommand "cmd.exe"
	Write-Pass "pane current command active child"
	Write-Pass "pane C-c interrupt"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    'powershell -NoProfile -Command "Start-Sleep -Seconds 30"',
	    "Enter") | Out-Null
	Wait-PaneCurrentCommand "pane C-c PowerShell active child" `
	    "smoke:0.0" "powershell.exe" | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0", "C-c") |
	    Out-Null
	Wait-PaneCurrentCommand "pane C-c PowerShell interrupt" `
	    "smoke:0.0" "cmd.exe" | Out-Null
	Write-Pass "pane C-c PowerShell interrupt"

	$etxScript = Join-Path $Temp "etx-byte.ps1"
	$etxReady = Join-Path $Temp "etx-ready.txt"
	$etxFile = Join-Path $Temp "etx-byte.txt"
	Set-Content -LiteralPath $etxScript -Encoding ascii -Value @'
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
	$etxCommand = "powershell -NoProfile -NonInteractive " +
	    "-ExecutionPolicy Bypass -File `"$etxScript`" " +
	    "-Ready `"$etxReady`" -Output `"$etxFile`""
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    $etxCommand, "Enter") | Out-Null
	$etxReadyWait = [Diagnostics.Stopwatch]::StartNew()
	while ($etxReadyWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $etxReady)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "pane C-c ETX ready" $etxReady "ready"
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0", "C-c") |
	    Out-Null
	$etxWait = [Diagnostics.Stopwatch]::StartNew()
	while ($etxWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $etxFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "pane C-c ETX byte" $etxFile `
	    "TMUX_WIN32_ETX_BYTE"
	Write-Pass "pane C-c ETX byte"

	$breakScript = Join-Path $Temp "ctrl-break.ps1"
	$breakReady = Join-Path $Temp "ctrl-break-ready.txt"
	$breakFile = Join-Path $Temp "ctrl-break.txt"
	Set-Content -LiteralPath $breakScript -Encoding ascii -Value @'
param([string]$Ready, [string]$Output)
$ErrorActionPreference = "Stop"
$source = @"
using System;
using System.Runtime.InteropServices;
public static class TmuxBreakHandler {
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
[void][TmuxBreakHandler]::SetConsoleCtrlHandler(
    [TmuxBreakHandler]::Handler, $true)
Set-Content -LiteralPath $Ready -Encoding ascii -Value "ready"
$deadline = [DateTime]::UtcNow.AddSeconds(10)
while ([DateTime]::UtcNow -lt $deadline) {
	if ([TmuxBreakHandler]::SeenBreak -ne 0) {
		Set-Content -LiteralPath $Output -Encoding ascii `
		    -Value "TMUX_WIN32_CTRL_BREAK"
		exit 0
	}
	Start-Sleep -Milliseconds 50
}
Set-Content -LiteralPath $Output -Encoding ascii `
    -Value "TMUX_WIN32_CTRL_BREAK_MISSING"
exit 2
'@
	$breakCommand = "powershell -NoProfile -NonInteractive " +
	    "-ExecutionPolicy Bypass -File `"$breakScript`" " +
	    "-Ready `"$breakReady`" -Output `"$breakFile`""
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    $breakCommand, "Enter") | Out-Null
	$breakReadyWait = [Diagnostics.Stopwatch]::StartNew()
	while ($breakReadyWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $breakReady)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "pane C-Break ready" $breakReady "ready"
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0", "C-Break") |
	    Out-Null
	$breakWait = [Diagnostics.Stopwatch]::StartNew()
	while ($breakWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $breakFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "pane C-Break control event" $breakFile `
	    "TMUX_WIN32_CTRL_BREAK"
	Write-Pass "pane C-Break control event"

	$panePath = (Invoke-SmokeTmux @("display-message", "-p", "-t",
	    "smoke:0.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $panePath)) {
		throw "pane_current_path did not resolve: $panePath"
	}
	Write-Pass "pane current metadata"

	$dynamicPaneCwd = Join-Path $Temp "dynamic-pane-cwd"
	New-Item -ItemType Directory -Force -Path $dynamicPaneCwd | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "cd /d `"$dynamicPaneCwd`"", "Enter", "cd", "Enter") | Out-Null
	$dynamicPathPollSw = [Diagnostics.Stopwatch]::StartNew()
	$dynamicPanePath = ""
	while ($dynamicPathPollSw.ElapsedMilliseconds -lt 9000) {
		$dynamicPanePath = (Invoke-SmokeTmux @("display-message", "-p",
		    "-t", "smoke:0.0", "#{pane_current_path}")).Out.Trim()
		$resolvedDynamicPanePath = Resolve-SmokePath $dynamicPanePath
		$resolvedDynamicPaneCwd = Resolve-SmokePath $dynamicPaneCwd
		if ($resolvedDynamicPanePath -eq $resolvedDynamicPaneCwd) { break }
		Start-Sleep -Milliseconds 100
	}
	$dynamicPanePath = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:0.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $dynamicPanePath)) {
		throw "dynamic pane_current_path did not resolve: $dynamicPanePath"
	}
	$resolvedDynamicPanePath = Resolve-SmokePath $dynamicPanePath
	$resolvedDynamicPaneCwd = Resolve-SmokePath $dynamicPaneCwd
	if ($resolvedDynamicPanePath -ne $resolvedDynamicPaneCwd) {
		throw "dynamic pane_current_path mismatch: $resolvedDynamicPanePath"
	}
	Write-Pass "pane current path dynamic cwd"

	$paneCwd = Join-Path $Temp "pane-cwd"
	New-Item -ItemType Directory -Force -Path $paneCwd | Out-Null
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "cwd", "-c", $paneCwd, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane cwd" "smoke:cwd.0" "cmd.exe" 7000 |
	    Out-Null
	$paneCwdPath = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:cwd.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $paneCwdPath)) {
		throw "new-window -c path did not resolve: $paneCwdPath"
	}
	$resolvedPaneCwd = Resolve-SmokePath $paneCwdPath
	$resolvedExpectedCwd = Resolve-SmokePath $paneCwd
	if ($resolvedPaneCwd -ne $resolvedExpectedCwd) {
		throw "new-window -c path mismatch: $resolvedPaneCwd"
	}
	$paneCwdWithSpaces = Join-Path $Temp "pane cwd with spaces (amp&one)"
	New-Item -ItemType Directory -Force -Path $paneCwdWithSpaces |
	    Out-Null
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "cwdspace", "-c", $paneCwdWithSpaces, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane cwd spaces" "smoke:cwdspace.0" `
	    "cmd.exe" 7000 | Out-Null
	$paneCwdWithSpacesPath = (Invoke-SmokeTmux @("display-message",
	    "-p", "-t", "smoke:cwdspace.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $paneCwdWithSpacesPath)) {
		throw ("new-window -c path with spaces did not resolve: " +
		    "$paneCwdWithSpacesPath")
	}
	$resolvedPaneCwdWithSpaces = Resolve-SmokePath $paneCwdWithSpacesPath
	$resolvedExpectedCwdWithSpaces = Resolve-SmokePath $paneCwdWithSpaces
	if ($resolvedPaneCwdWithSpaces -ne $resolvedExpectedCwdWithSpaces) {
		throw ("new-window -c path with spaces mismatch: " +
		    "$resolvedPaneCwdWithSpaces")
	}
	$paneJunctionTarget = Join-Path $Temp "pane-junction-target"
	$paneJunctionCwd = Join-Path $Temp "pane junction cwd"
	New-Item -ItemType Directory -Force -Path $paneJunctionTarget |
	    Out-Null
	New-Item -ItemType Junction -Path $paneJunctionCwd `
	    -Target $paneJunctionTarget | Out-Null
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "cwdjunction", "-c", $paneJunctionCwd, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane cwd junction" "smoke:cwdjunction.0" `
	    "cmd.exe" 7000 | Out-Null
	$paneJunctionPath = (Invoke-SmokeTmux @("display-message",
	    "-p", "-t", "smoke:cwdjunction.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $paneJunctionPath)) {
		throw "new-window -c junction cwd did not resolve: $paneJunctionPath"
	}
	$resolvedPaneJunctionPath = Resolve-SmokePath $paneJunctionPath
	$resolvedExpectedJunctionCwd = Resolve-SmokePath $paneJunctionCwd
	if ($resolvedPaneJunctionPath -ne $resolvedExpectedJunctionCwd) {
		throw "new-window -c junction cwd mismatch: $resolvedPaneJunctionPath"
	}
	$paneSymlinkCreated = $false
	$paneSymlinkTarget = Join-Path $Temp "pane-symlink-target"
	$paneSymlinkCwd = Join-Path $Temp "pane symlink cwd"
	New-Item -ItemType Directory -Force -Path $paneSymlinkTarget |
	    Out-Null
	try {
		New-Item -ItemType SymbolicLink -Path $paneSymlinkCwd `
		    -Target $paneSymlinkTarget -ErrorAction Stop | Out-Null
		$paneSymlinkCreated = Test-Path -LiteralPath $paneSymlinkCwd
	} catch {
		Write-Host "[SKIP] pane symlink cwd: $($_.Exception.Message)"
	}
	if ($paneSymlinkCreated) {
		Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke",
		    "-n", "cwdsymlink", "-c", $paneSymlinkCwd, "cmd.exe") |
		    Out-Null
		Wait-PaneCurrentCommand "pane cwd symlink" "smoke:cwdsymlink.0" `
		    "cmd.exe" 7000 | Out-Null
		$paneSymlinkPath = (Invoke-SmokeTmux @("display-message",
		    "-p", "-t", "smoke:cwdsymlink.0",
		    "#{pane_current_path}")).Out.Trim()
		if (-not (Test-Path -LiteralPath $paneSymlinkPath)) {
			throw ("new-window -c symlink cwd did not resolve: " +
			    "$paneSymlinkPath")
		}
		$resolvedPaneSymlinkPath = Resolve-SmokePath $paneSymlinkPath
		$resolvedExpectedSymlinkCwd = Resolve-SmokePath $paneSymlinkCwd
		if ($resolvedPaneSymlinkPath -ne $resolvedExpectedSymlinkCwd) {
			throw ("new-window -c symlink cwd mismatch: " +
			    "$resolvedPaneSymlinkPath")
		}
	}
	$paneExtendedDir = Join-Path $Temp "pane-extended-cwd"
	New-Item -ItemType Directory -Force -Path $paneExtendedDir |
	    Out-Null
	$paneExtendedCwd = '\\?\' + (Resolve-Path -LiteralPath `
	    $paneExtendedDir).Path
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "cwdextended", "-c", $paneExtendedCwd, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane cwd extended" "smoke:cwdextended.0" `
	    "cmd.exe" 7000 | Out-Null
	$paneExtendedPath = (Invoke-SmokeTmux @("display-message",
	    "-p", "-t", "smoke:cwdextended.0",
	    "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $paneExtendedPath)) {
		throw "new-window -c extended cwd did not resolve: $paneExtendedPath"
	}
	$resolvedPaneExtendedPath = Resolve-SmokePath $paneExtendedPath
	$resolvedExpectedExtendedCwd = Resolve-SmokePath $paneExtendedDir
	if ($resolvedPaneExtendedPath -ne $resolvedExpectedExtendedCwd) {
		throw "new-window -c extended cwd mismatch: $resolvedPaneExtendedPath"
	}
	$paneLong = New-LongSmokeDirectory $Temp "pane-long-cwd"
	$paneLongFile = Join-Path $paneLong.Path "pane-long.txt"
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "cwdlong", "-c", $paneLong.Path, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane cwd long" "smoke:cwdlong.0" `
	    "cmd.exe" 7000 | Out-Null
	$paneLongPath = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:cwdlong.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-SmokePath $paneLongPath)) {
		throw "new-window -c long cwd did not resolve: $paneLongPath"
	}
	$resolvedPaneLongPath = Resolve-SmokePath $paneLongPath
	$resolvedExpectedLongCwd = Resolve-SmokePath $paneLong.ExtendedPath
	if ($resolvedPaneLongPath -ne $resolvedExpectedLongCwd) {
		throw "new-window -c long cwd mismatch: $resolvedPaneLongPath"
	}
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:cwdlong.0",
	    "echo TMUX_WIN32_CMD_LONG_CWD>pane-long.txt", "Enter") |
	    Out-Null
	Wait-FileContains "cmd.exe pane long cwd" $paneLongFile `
	    "TMUX_WIN32_CMD_LONG_CWD"
	$paneUncWindowCreated = $false
	$paneUncDir = Join-Path $Temp "pane-unc-cwd"
	New-Item -ItemType Directory -Force -Path $paneUncDir | Out-Null
	$paneUncResolved = (Resolve-Path -LiteralPath $paneUncDir).Path
	$paneUncDrive = ([System.IO.Path]::GetPathRoot(
	    $paneUncResolved)).Substring(0, 1)
	$paneUncRest = $paneUncResolved.Substring(3)
	$paneUncCwd = "\\localhost\$paneUncDrive`$\" + $paneUncRest
	if (Test-Path -LiteralPath $paneUncCwd) {
		$paneUncWindowCreated = $true
		$paneUncFile = Join-Path $paneUncDir "cmd-pane-unc.txt"
		Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke",
		    "-n", "cwdunc", "-c", $paneUncCwd) | Out-Null
		Wait-PaneCurrentCommand "pane cwd UNC" "smoke:cwdunc.0" `
		    "cmd.exe" 9000 | Out-Null
		Invoke-SmokeTmux @("send-keys", "-t", "smoke:cwdunc.0",
		    "echo TMUX_WIN32_CMD_UNC>cmd-pane-unc.txt", "Enter") |
		    Out-Null
		Wait-FileContains "cmd.exe pane UNC cwd" $paneUncFile `
		    "TMUX_WIN32_CMD_UNC"
	} else {
		Write-Host "[SKIP] cmd.exe pane UNC cwd unavailable: $paneUncCwd"
	}
	$missingPaneCwd = Join-Path $Temp "missing-pane-cwd"
	Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
	    "badcwd", "-c", $missingPaneCwd, "cmd.exe") | Out-Null
	Wait-PaneCurrentCommand "pane bad cwd" "smoke:badcwd.0" `
	    "cmd.exe" 7000 | Out-Null
	$fallbackPaneCwd = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:badcwd.0", "#{pane_current_path}")).Out.Trim()
	if (-not (Test-Path -LiteralPath $fallbackPaneCwd)) {
		throw "invalid new-window -c fallback did not resolve: $fallbackPaneCwd"
	}
	Close-WindowGracefully "pane cwd cleanup" "smoke:cwd"
	Close-WindowGracefully "pane cwd spaces cleanup" "smoke:cwdspace"
	Close-WindowGracefully "pane cwd junction cleanup" `
	    "smoke:cwdjunction"
	if ($paneSymlinkCreated) {
		Close-WindowGracefully "pane cwd symlink cleanup" `
		    "smoke:cwdsymlink"
	}
	Close-WindowGracefully "pane cwd extended cleanup" `
	    "smoke:cwdextended"
	Close-WindowGracefully "pane cwd long cleanup" "smoke:cwdlong"
	if ($paneUncWindowCreated) {
		Close-WindowGracefully "pane cwd UNC cleanup" "smoke:cwdunc"
	}
	Close-WindowGracefully "pane badcwd cleanup" "smoke:badcwd"
	Write-Pass "pane cwd selection"

	$powershellShell = Join-Path $env:SystemRoot `
	    "System32\WindowsPowerShell\v1.0\powershell.exe"
	if (-not (Test-Path -LiteralPath $powershellShell)) {
		throw "PowerShell shell not found: $powershellShell"
	}
	$originalDefaultShell = (Invoke-SmokeTmux @("show-option", "-gqv",
	    "default-shell")).Out.Trim()
	$powershellUncWindowCreated = $false
	try {
		Invoke-SmokeTmux @("set-option", "-g", "default-shell",
		    $powershellShell) | Out-Null
		Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
		    "psshell") | Out-Null
		Wait-PaneCurrentCommand "PowerShell default-shell pane" `
		    "smoke:psshell.0" "powershell.exe" | Out-Null
		$powershellCommand = "Write-Output " +
		    "TMUX_WIN32_POWERSHELL_COMMAND; Start-Sleep -Seconds 10"
		Invoke-SmokeTmux @("new-window", "-d", "-t", "smoke", "-n",
		    "pscmd", $powershellCommand) | Out-Null
		$psCommandCapture = ""
		$psCommandWait = [Diagnostics.Stopwatch]::StartNew()
		while ($psCommandWait.ElapsedMilliseconds -lt 10000) {
			$psCommandCapture = (Invoke-SmokeTmux @(
			    "capture-pane", "-p", "-t", "smoke:pscmd.0")).Out
			if ($psCommandCapture -like
			    "*TMUX_WIN32_POWERSHELL_COMMAND*") {
				break
			}
			Start-Sleep -Milliseconds 300
		}
		Assert-Contains "PowerShell default-shell command" `
		    $psCommandCapture "TMUX_WIN32_POWERSHELL_COMMAND"
		$powershellUncDir = Join-Path $Temp "powershell-unc-cwd"
		New-Item -ItemType Directory -Force -Path $powershellUncDir |
		    Out-Null
		$powershellUncResolved = (Resolve-Path -LiteralPath `
		    $powershellUncDir).Path
		$powershellUncDrive = ([System.IO.Path]::GetPathRoot(
		    $powershellUncResolved)).Substring(0, 1)
		$powershellUncRest = $powershellUncResolved.Substring(3)
		$powershellUncCwd = "\\localhost\$powershellUncDrive`$\" +
		    $powershellUncRest
		if (Test-Path -LiteralPath $powershellUncCwd) {
			$powershellUncWindowCreated = $true
			$powershellUncMarker = Join-Path $powershellUncDir `
			    "unc-marker.txt"
			$powershellUncCommand = "Set-Content -LiteralPath " +
			    "unc-marker.txt -Encoding ascii -Value " +
			    "TMUX_WIN32_POWERSHELL_UNC; Start-Sleep -Seconds 10"
			Invoke-SmokeTmux @("new-window", "-d", "-t",
			    "smoke", "-n", "psunc", "-c", $powershellUncCwd,
			    $powershellUncCommand) | Out-Null
			Wait-FileContains "PowerShell default-shell UNC cwd" `
			    $powershellUncMarker "TMUX_WIN32_POWERSHELL_UNC"
		} else {
			Write-Host ("[SKIP] PowerShell UNC cwd unavailable: " +
			    $powershellUncCwd)
		}
	} finally {
		try {
			Close-WindowGracefully "PowerShell shell cleanup" `
			    "smoke:psshell"
		} catch {
		}
		try {
			Close-WindowGracefully "PowerShell command cleanup" `
			    "smoke:pscmd"
		} catch {
		}
		if ($powershellUncWindowCreated) {
			try {
				Close-WindowGracefully "PowerShell UNC cleanup" `
				    "smoke:psunc"
			} catch {
			}
		}
		if (-not [string]::IsNullOrEmpty($originalDefaultShell)) {
			Invoke-SmokeTmux @("set-option", "-g",
			    "default-shell", $originalDefaultShell) | Out-Null
		}
	}
	Write-Pass "PowerShell default-shell"

	$runShellFile = Join-Path $Temp "run-shell.txt"
	$ifShellFile = Join-Path $Temp "if-shell.txt"
	$runShellTarget = $runShellFile.Replace('\', '/')
	$ifShellTarget = $ifShellFile.Replace('\', '/')
	Invoke-SmokeTmux @("run-shell",
	    "echo TMUX_WIN32_RUN_SHELL>$runShellTarget") | Out-Null
	Assert-FileContains "run-shell" $runShellFile "TMUX_WIN32_RUN_SHELL"
	$runShellQuotedDir = Join-Path $Temp `
	    "run shell target with spaces (amp&one)"
	New-Item -ItemType Directory -Force -Path $runShellQuotedDir |
	    Out-Null
	$runShellQuotedFile = Join-Path $runShellQuotedDir `
	    "run shell quoted target.txt"
	$runShellQuotedTarget = $runShellQuotedFile.Replace('\', '/')
	Invoke-SmokeTmux @("run-shell",
	    "echo TMUX_WIN32_RUN_SHELL_QUOTED>`"$runShellQuotedTarget`"") |
	    Out-Null
	Assert-FileContains "run-shell quoted target" $runShellQuotedFile `
	    "TMUX_WIN32_RUN_SHELL_QUOTED"
	Write-Pass "run-shell"

	$runShellOutput = (Invoke-SmokeTmux @("run-shell",
	    "echo TMUX_WIN32_RUN_SHELL_STDOUT")).Out
	Assert-Contains "run-shell stdout" $runShellOutput `
	    "TMUX_WIN32_RUN_SHELL_STDOUT"
	$runShellMixedCommand = "for /l %i in (1,1,8) do @(" +
	    "echo TMUX_WIN32_JOB_OUT_%i & " +
	    "echo TMUX_WIN32_JOB_ERR_%i 1>&2)"
	$runShellMixedOutput = (Invoke-SmokeTmux @("run-shell", "-E",
	    $runShellMixedCommand)).Out
	Assert-Contains "run-shell stdout/stderr" $runShellMixedOutput `
	    "TMUX_WIN32_JOB_OUT_1"
	Assert-Contains "run-shell stdout/stderr" $runShellMixedOutput `
	    "TMUX_WIN32_JOB_ERR_1"
	Assert-Contains "run-shell stdout/stderr" $runShellMixedOutput `
	    "TMUX_WIN32_JOB_OUT_8"
	Assert-Contains "run-shell stdout/stderr" $runShellMixedOutput `
	    "TMUX_WIN32_JOB_ERR_8"
	Write-Pass "run-shell stdout/stderr"

	$runShellBackgroundFile = Join-Path $Temp "run-shell-background.txt"
	$runShellBackgroundTarget = $runShellBackgroundFile.Replace('\', '/')
	Invoke-SmokeTmux @("run-shell", "-b",
	    "ping -n 3 127.0.0.1 >NUL & echo TMUX_WIN32_RUN_SHELL_B>$runShellBackgroundTarget") |
	    Out-Null
	$runShellBackgroundWait = [Diagnostics.Stopwatch]::StartNew()
	while ($runShellBackgroundWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $runShellBackgroundFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "run-shell -b" $runShellBackgroundFile `
	    "TMUX_WIN32_RUN_SHELL_B"
	Write-Pass "run-shell -b"

	$jobCwd = Join-Path $Temp "job-cwd"
	New-Item -ItemType Directory -Force -Path $jobCwd | Out-Null
	$jobCwdFile = Join-Path $jobCwd "run-shell-cwd.txt"
	Invoke-SmokeTmux @("run-shell", "-c", $jobCwd,
	    "echo TMUX_WIN32_RUN_SHELL_CWD>run-shell-cwd.txt") | Out-Null
	Assert-FileContains "run-shell cwd" $jobCwdFile `
	    "TMUX_WIN32_RUN_SHELL_CWD"
	$jobCwdWithSpaces = Join-Path $Temp "job cwd with spaces (amp&one)"
	New-Item -ItemType Directory -Force -Path $jobCwdWithSpaces |
	    Out-Null
	$jobCwdWithSpacesFile = Join-Path $jobCwdWithSpaces `
	    "run-shell-cwd-spaces.txt"
	Invoke-SmokeTmux @("run-shell", "-c", $jobCwdWithSpaces,
	    "echo TMUX_WIN32_RUN_SHELL_CWD_SPACES>run-shell-cwd-spaces.txt") |
	    Out-Null
	Assert-FileContains "run-shell cwd with spaces" $jobCwdWithSpacesFile `
	    "TMUX_WIN32_RUN_SHELL_CWD_SPACES"
	$jobJunctionTarget = Join-Path $Temp "job-junction-target"
	$jobJunctionCwd = Join-Path $Temp "job junction cwd"
	New-Item -ItemType Directory -Force -Path $jobJunctionTarget |
	    Out-Null
	New-Item -ItemType Junction -Path $jobJunctionCwd `
	    -Target $jobJunctionTarget | Out-Null
	$jobJunctionFile = Join-Path $jobJunctionCwd `
	    "run-shell-cwd-junction.txt"
	Invoke-SmokeTmux @("run-shell", "-c", $jobJunctionCwd,
	    "echo TMUX_WIN32_RUN_SHELL_CWD_JUNCTION>run-shell-cwd-junction.txt") |
	    Out-Null
	Assert-FileContains "run-shell cwd junction" $jobJunctionFile `
	    "TMUX_WIN32_RUN_SHELL_CWD_JUNCTION"
	$jobSymlinkCreated = $false
	$jobSymlinkTarget = Join-Path $Temp "job-symlink-target"
	$jobSymlinkCwd = Join-Path $Temp "job symlink cwd"
	New-Item -ItemType Directory -Force -Path $jobSymlinkTarget |
	    Out-Null
	try {
		New-Item -ItemType SymbolicLink -Path $jobSymlinkCwd `
		    -Target $jobSymlinkTarget -ErrorAction Stop | Out-Null
		$jobSymlinkCreated = Test-Path -LiteralPath $jobSymlinkCwd
	} catch {
		Write-Host "[SKIP] job symlink cwd: $($_.Exception.Message)"
	}
	if ($jobSymlinkCreated) {
		$jobSymlinkFile = Join-Path $jobSymlinkCwd `
		    "run-shell-cwd-symlink.txt"
		Invoke-SmokeTmux @("run-shell", "-c", $jobSymlinkCwd,
		    "echo TMUX_WIN32_RUN_SHELL_CWD_SYMLINK>run-shell-cwd-symlink.txt") |
		    Out-Null
		Assert-FileContains "run-shell cwd symlink" $jobSymlinkFile `
		    "TMUX_WIN32_RUN_SHELL_CWD_SYMLINK"
	}
	$jobExtendedDir = Join-Path $Temp "job-extended-cwd"
	New-Item -ItemType Directory -Force -Path $jobExtendedDir |
	    Out-Null
	$jobExtendedCwd = '\\?\' + (Resolve-Path -LiteralPath `
	    $jobExtendedDir).Path
	$jobExtendedFile = Join-Path $jobExtendedDir `
	    "run-shell-cwd-extended.txt"
	Invoke-SmokeTmux @("run-shell", "-c", $jobExtendedCwd,
	    "echo TMUX_WIN32_RUN_SHELL_CWD_EXTENDED>run-shell-cwd-extended.txt") |
	    Out-Null
	Assert-FileContains "run-shell cwd extended" $jobExtendedFile `
	    "TMUX_WIN32_RUN_SHELL_CWD_EXTENDED"
	$jobLong = New-LongSmokeDirectory $Temp "job-long-cwd"
	$jobLongFile = Join-Path $jobLong.Path "job-long.txt"
	Invoke-SmokeTmux @("run-shell", "-c", $jobLong.Path,
	    "echo TMUX_WIN32_RUN_SHELL_CWD_LONG>job-long.txt") |
	    Out-Null
	Assert-FileContains "run-shell cwd long" $jobLongFile `
	    "TMUX_WIN32_RUN_SHELL_CWD_LONG"
	$jobUncDir = Join-Path $Temp "job-unc-cwd"
	New-Item -ItemType Directory -Force -Path $jobUncDir | Out-Null
	$jobUncResolved = (Resolve-Path -LiteralPath $jobUncDir).Path
	$jobUncDrive = ([System.IO.Path]::GetPathRoot(
	    $jobUncResolved)).Substring(0, 1)
	$jobUncRest = $jobUncResolved.Substring(3)
	$jobUncCwd = "\\localhost\$jobUncDrive`$\" + $jobUncRest
	if (Test-Path -LiteralPath $jobUncCwd) {
		$jobUncFile = Join-Path $jobUncDir "run-shell-cwd-unc.txt"
		Invoke-SmokeTmux @("run-shell", "-c", $jobUncCwd,
		    "echo TMUX_WIN32_RUN_SHELL_CWD_UNC>run-shell-cwd-unc.txt") |
		    Out-Null
		Assert-FileContains "run-shell cwd UNC" $jobUncFile `
		    "TMUX_WIN32_RUN_SHELL_CWD_UNC"
	} else {
		Write-Host "[SKIP] run-shell UNC cwd unavailable: $jobUncCwd"
	}
	$missingJobCwd = Join-Path $Temp "missing-job-cwd"
	$badJobCwdFile = Join-Path $Temp "run-shell-bad-cwd.txt"
	$badJobCwdTarget = $badJobCwdFile.Replace('\', '/')
	Invoke-SmokeTmux @("run-shell", "-c", $missingJobCwd,
	    "echo TMUX_WIN32_RUN_SHELL_BAD_CWD>$badJobCwdTarget") |
	    Out-Null
	Assert-FileContains "run-shell invalid cwd" $badJobCwdFile `
	    "TMUX_WIN32_RUN_SHELL_BAD_CWD"
	Write-Pass "run-shell cwd"

	Invoke-SmokeTmux @("if-shell", "cmd /c exit 0",
	    "run-shell `"echo TMUX_WIN32_IF_SHELL>$ifShellTarget`"") |
	    Out-Null
	Assert-FileContains "if-shell" $ifShellFile "TMUX_WIN32_IF_SHELL"
	Write-Pass "if-shell"

	$pipeFile = Join-Path $Temp "pipe-pane.txt"
	$pipeTarget = $pipeFile.Replace('\', '/')
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0",
	    "more > $pipeTarget") | Out-Null
	Start-Sleep -Milliseconds 300
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_PIPE_PANE", "Enter") | Out-Null
	Start-Sleep -Milliseconds 900
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0") | Out-Null
	Start-Sleep -Milliseconds 900
	Assert-FileContains "pipe-pane" $pipeFile "TMUX_WIN32_PIPE_PANE"
	Write-Pass "pipe-pane output"

	$pipeQuotedDir = Join-Path $Temp "pipe target with spaces (amp&one)"
	New-Item -ItemType Directory -Force -Path $pipeQuotedDir | Out-Null
	$pipeQuotedFile = Join-Path $pipeQuotedDir "pipe-pane quoted target.txt"
	$pipeQuotedTarget = $pipeQuotedFile.Replace('\', '/')
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0",
	    "more > `"$pipeQuotedTarget`"") | Out-Null
	Start-Sleep -Milliseconds 300
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_PIPE_PANE_QUOTED", "Enter") | Out-Null
	Start-Sleep -Milliseconds 900
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0") | Out-Null
	Start-Sleep -Milliseconds 900
	Assert-FileContains "pipe-pane quoted target" $pipeQuotedFile `
	    "TMUX_WIN32_PIPE_PANE_QUOTED"
	Write-Pass "pipe-pane quoted target"

	$pipeBulkFile = Join-Path $Temp "pipe-pane-bulk.txt"
	$pipeBulkTarget = $pipeBulkFile.Replace('\', '/')
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0",
	    "more > $pipeBulkTarget") | Out-Null
	Start-Sleep -Milliseconds 300
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "for /l %i in (1,1,160) do @echo TMUX_WIN32_PIPE_BULK_%i",
	    "Enter") | Out-Null
	$pipeBulkWait = [Diagnostics.Stopwatch]::StartNew()
	$pipeBulkText = ""
	while ($pipeBulkWait.ElapsedMilliseconds -lt 12000) {
		if (Test-Path -LiteralPath $pipeBulkFile) {
			$pipeBulkText = Get-Content -LiteralPath `
			    $pipeBulkFile -Raw
			if ($pipeBulkText -like "*TMUX_WIN32_PIPE_BULK_160*") {
				break
			}
		}
		Start-Sleep -Milliseconds 200
	}
	Invoke-SmokeTmux @("pipe-pane", "-t", "smoke:0.0") | Out-Null
	Start-Sleep -Milliseconds 700
	if (Test-Path -LiteralPath $pipeBulkFile) {
		$pipeBulkText = Get-Content -LiteralPath $pipeBulkFile -Raw
	}
	Assert-Contains "pipe-pane bulk output" $pipeBulkText `
	    "TMUX_WIN32_PIPE_BULK_1"
	Assert-Contains "pipe-pane bulk output" $pipeBulkText `
	    "TMUX_WIN32_PIPE_BULK_160"
	$pipeBulkCount = ([regex]::Matches($pipeBulkText,
	    "TMUX_WIN32_PIPE_BULK_")).Count
	if ($pipeBulkCount -lt 150) {
		throw "pipe-pane bulk output captured only $pipeBulkCount lines"
	}
	Write-Pass "pipe-pane bulk output"

	Invoke-SmokeTmux @("pipe-pane", "-I", "-t", "smoke:0.0",
	    "echo echo TMUX_WIN32_PIPE_I") | Out-Null
	$capture = Wait-PaneContains "pipe-pane -I" "smoke:0.0" `
	    "TMUX_WIN32_PIPE_I" 12000
	Assert-Contains "pipe-pane -I" $capture "TMUX_WIN32_PIPE_I"
	Write-Pass "pipe-pane -I"

	Invoke-SmokeTmux @("pipe-pane", "-IO", "-t", "smoke:0.0",
	    "cmd /v /c set /p L=& echo echo TMUX_WIN32_PIPE_IO") |
	    Out-Null
	Start-Sleep -Milliseconds 300
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_PIPE_IO_TRIGGER", "Enter") | Out-Null
	$capture = Wait-PaneContains "pipe-pane -IO" "smoke:0.0" `
	    "TMUX_WIN32_PIPE_IO" 12000
	Assert-Contains "pipe-pane -IO" $capture "TMUX_WIN32_PIPE_IO"
	Write-Pass "pipe-pane -IO"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_COPY_TARGET", "Enter") | Out-Null
	Wait-PaneContains "copy-mode target" "smoke:0.0" `
	    "TMUX_WIN32_COPY_TARGET" 8000 | Out-Null
	Invoke-SmokeTmux @("copy-mode", "-t", "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-backward", "TMUX_WIN32_COPY_TARGET") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "copy-line-and-cancel") | Out-Null
	$buffer = (Invoke-SmokeTmux @("show-buffer")).Out
	Assert-Contains "copy-mode" $buffer "TMUX_WIN32_COPY_TARGET"
	Write-Pass "copy-mode copy-line"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_SEARCH_ALPHA", "Enter") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_SEARCH_BETA", "Enter") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_SEARCH_GAMMA", "Enter") | Out-Null
	Wait-PaneContains "search gamma" "smoke:0.0" `
	    "TMUX_WIN32_SEARCH_GAMMA" 9000 | Out-Null
	Invoke-SmokeTmux @("copy-mode", "-t", "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-backward", "TMUX_WIN32_SEARCH_ALPHA") | Out-Null
	$searchLine = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:0.0", "#{copy_cursor_line}")).Out
	Assert-Contains "copy-mode search backward" $searchLine `
	    "TMUX_WIN32_SEARCH_ALPHA"
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-forward", "TMUX_WIN32_SEARCH_GAMMA") | Out-Null
	$searchLine = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:0.0", "#{copy_cursor_line}")).Out
	Assert-Contains "copy-mode search forward" $searchLine `
	    "TMUX_WIN32_SEARCH_GAMMA"
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "cancel") | Out-Null
	Write-Pass "copy-mode search navigation"

	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_COPY_MULTI_A", "Enter") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_COPY_MULTI_B", "Enter") | Out-Null
	Wait-PaneContains "copy multi B" "smoke:0.0" `
	    "TMUX_WIN32_COPY_MULTI_B" 9000 | Out-Null
	Invoke-SmokeTmux @("copy-mode", "-t", "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-backward", "TMUX_WIN32_COPY_MULTI_A") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-N", "4", "-t",
	    "smoke:0.0", "select-line") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "copy-selection-and-cancel") | Out-Null
	$multiBuffer = (Invoke-SmokeTmux @("show-buffer")).Out
	Assert-Contains "copy-mode multi-line" $multiBuffer `
	    "TMUX_WIN32_COPY_MULTI_A"
	Assert-Contains "copy-mode multi-line" $multiBuffer `
	    "TMUX_WIN32_COPY_MULTI_B"
	Write-Pass "copy-mode multi-line selection"

	$rectangleCommand = "echo AAAA_RECT_ONE & echo BBBB_RECT_TWO"
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    $rectangleCommand, "Enter") | Out-Null
	$rectangleCapture = Wait-PaneContains "copy-mode rectangle source" `
	    "smoke:0.0" "AAAA_RECT_ONE" 12000
	Assert-Contains "copy-mode rectangle source" $rectangleCapture `
	    "AAAA_RECT_ONE"
	Assert-Contains "copy-mode rectangle source" $rectangleCapture `
	    "BBBB_RECT_TWO"
	Invoke-SmokeTmux @("copy-mode", "-t", "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-backward", "AAAA_RECT_ONE") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "begin-selection") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "rectangle-on") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "cursor-down") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-N", "4", "-t",
	    "smoke:0.0", "cursor-right") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "copy-selection-and-cancel") | Out-Null
	$rectangleBuffer = (Invoke-SmokeTmux @("show-buffer")).Out
	Assert-Contains "copy-mode rectangle" $rectangleBuffer "AAAA"
	Assert-Contains "copy-mode rectangle" $rectangleBuffer "BBBB"
	Write-Pass "copy-mode rectangle selection"

	Invoke-SmokeTmux @("set-buffer", "-b", "codexpaste",
	    "echo TMUX_WIN32_PASTE_BUFFER") | Out-Null
	Invoke-SmokeTmux @("paste-buffer", "-b", "codexpaste", "-t",
	    "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0", "Enter") |
	    Out-Null
	$capture = Wait-PaneContains "paste-buffer" "smoke:0.0" `
	    "TMUX_WIN32_PASTE_BUFFER" 9000
	Assert-Contains "paste-buffer" $capture "TMUX_WIN32_PASTE_BUFFER"
	Write-Pass "paste-buffer"

	$copyPipeFile = Join-Path $Temp "copy-pipe.txt"
	$copyPipeTarget = $copyPipeFile.Replace('\', '/')
	Invoke-SmokeTmux @("copy-mode", "-t", "smoke:0.0") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "search-backward", "TMUX_WIN32_COPY_TARGET") | Out-Null
	Invoke-SmokeTmux @("send-keys", "-X", "-t", "smoke:0.0",
	    "copy-pipe-line-and-cancel", "more > $copyPipeTarget") |
	    Out-Null
	Wait-FileContains "copy-pipe" $copyPipeFile "TMUX_WIN32_COPY_TARGET" 12000
	Write-Pass "copy-pipe-line"

	Start-ControlClient
	Read-ControlUntil "control-mode attach" "%session-changed" | Out-Null
	Send-ControlCommand 'display-message -p "#{session_name}"'
	Read-ControlUntil "control-mode display-message" "smoke" | Out-Null
	Send-ControlCommand 'refresh-client -B "codex::#{session_name}"'
	Read-ControlUntil "control-mode subscription" `
	    "%subscription-changed codex" | Out-Null
	Invoke-SmokeTmux @("rename-session", "-t", "smoke", "smokesub") |
	    Out-Null
	Read-ControlUntil "control-mode subscription rename" "smokesub" |
	    Out-Null
	Invoke-SmokeTmux @("rename-session", "-t", "smokesub", "smoke") |
	    Out-Null
	Write-Pass "control-mode subscription"
	$controlWindowId = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:0", "#{window_id}")).Out.Trim()
	Send-ControlCommand 'refresh-client -C 100x30'
	Send-ControlCommand `
	    'display-message -p "CTRL_RESIZE=#{client_width}:#{window_width}x#{window_height}"'
	Read-ControlUntil "control-mode client resize" `
	    "CTRL_RESIZE=100:100x30" | Out-Null
	Send-ControlCommand ("refresh-client -C {0}:90x25" -f
	    $controlWindowId)
	Send-ControlCommand `
	    'display-message -p "CTRL_WINDOW_RESIZE=#{window_width}x#{window_height}"'
	Read-ControlUntil "control-mode window resize" `
	    "CTRL_WINDOW_RESIZE=90x25" | Out-Null
	Send-ControlCommand ("refresh-client -C {0}:" -f $controlWindowId)
	Send-ControlCommand `
	    'display-message -p "CTRL_WINDOW_CLEAR=#{window_width}x#{window_height}"'
	Read-ControlUntil "control-mode window resize clear" `
	    "CTRL_WINDOW_CLEAR=100x30" | Out-Null
	Write-Pass "control-mode resize"
	$controlPaneId = (Invoke-SmokeTmux @("display-message", "-p",
	    "-t", "smoke:0.0", "#{pane_id}")).Out.Trim()
	$controlClients = (Invoke-SmokeTmux @("list-clients", "-F",
	    "#{client_name}:#{client_control_mode}:#{client_session}")).Out
	$controlClient = ($controlClients -split "`r?`n" | Where-Object {
	    $_ -like "*:1:smoke"
	} | Select-Object -First 1)
	if ([string]::IsNullOrEmpty($controlClient)) {
		throw "control client not found"
	}
	$controlClientName = ($controlClient -split ":", 3)[0]
	$windowSubscription = 'refresh-client -B "wincheck:' +
	    $controlWindowId + ':WIN=#{window_width}x#{window_height}"'
	Send-ControlCommand $windowSubscription
	Read-ControlUntil "control-mode window subscription initial" `
	    "WIN=" | Out-Null
	Send-ControlCommand ("refresh-client -C {0}:94x27" -f
	    $controlWindowId)
	Read-ControlUntil "control-mode window subscription resize" `
	    "WIN=94x27" | Out-Null
	Send-ControlCommand ("refresh-client -C {0}:" -f $controlWindowId)
	Send-ControlCommand `
	    'display-message -p "CTRL_WINDOW_RESUB_CLEAR=#{window_width}x#{window_height}"'
	Read-ControlUntil "control-mode window subscription clear" `
	    "CTRL_WINDOW_RESUB_CLEAR=100x30" | Out-Null
	Write-Pass "control-mode window subscription"
	$paneSubscription = 'refresh-client -B "panecheck:' +
	    $controlPaneId + ':PANEWIDTH=#{pane_width}"'
	Send-ControlCommand $paneSubscription
	Read-ControlUntil "control-mode pane subscription initial" `
	    "PANEWIDTH=" | Out-Null
	Invoke-SmokeTmux @("resize-pane", "-x", "54", "-t",
	    "smoke:0.0") | Out-Null
	Read-ControlUntil "control-mode pane subscription resize" `
	    "PANEWIDTH=54" | Out-Null
	Write-Pass "control-mode pane subscription"
	Drain-ControlOutput
	Invoke-SmokeTmux @("refresh-client", "-t", $controlClientName,
	    "-A", ("{0}:pause" -f $controlPaneId)) | Out-Null
	Read-ControlUntil "control-mode pane pause" `
	    ("%pause {0}" -f $controlPaneId) | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_CONTROL_PAUSE", "Enter") | Out-Null
	Start-Sleep -Milliseconds 500
	Invoke-SmokeTmux @("refresh-client", "-t", $controlClientName,
	    "-A", ("{0}:continue" -f $controlPaneId)) | Out-Null
	Read-ControlUntil "control-mode pane continue" `
	    ("%continue {0}" -f $controlPaneId) | Out-Null
	Invoke-SmokeTmux @("send-keys", "-t", "smoke:0.0",
	    "echo TMUX_WIN32_CONTROL_AFTER_CONTINUE", "Enter") | Out-Null
	Read-ControlUntil "control-mode pane resumed output" `
	    "TMUX_WIN32_CONTROL_AFTER_CONTINUE" | Out-Null
	Write-Pass "control-mode pane flow control"
	Send-ControlCommand 'send-keys -t smoke:0.0 -l "echo TMUX_WIN32_CONTROL_MODE"'
	Send-ControlCommand 'send-keys -t smoke:0.0 -H 0d'
	Start-Sleep -Milliseconds 1000
	Send-ControlCommand 'capture-pane -p -t smoke:0.0'
	Read-ControlUntil "control-mode capture-pane" `
	    "TMUX_WIN32_CONTROL_MODE" | Out-Null
	Send-ControlCommand ""
	$ControlProcess.WaitForExit(3000) | Out-Null
	Stop-ControlClient
	Write-Pass "control-mode attach"

	Invoke-SmokeTmux @("new-session", "-d", "-s", "attachsmoke",
	    "cmd.exe") | Out-Null
	$attachSmokePollSw = [Diagnostics.Stopwatch]::StartNew()
	while ($attachSmokePollSw.ElapsedMilliseconds -lt 7000) {
		$attachSessions = (Invoke-SmokeTmux @("list-sessions")).Out
		if ($attachSessions -like "*attachsmoke:*") { break }
		Start-Sleep -Milliseconds 100
	}
	Start-AttachedClient "attachsmoke"
	$attachReadyPollSw = [Diagnostics.Stopwatch]::StartNew()
	while ($attachReadyPollSw.ElapsedMilliseconds -lt 12000) {
		if ($AttachedProcess.HasExited) { break }
		Start-Sleep -Milliseconds 100
	}
	if ($AttachedProcess.HasExited) {
		$AttachedErrorTask.Wait(1000) | Out-Null
		$stderr = if ($AttachedErrorTask.IsCompleted) {
			$AttachedErrorTask.Result
		} else {
			""
		}
		throw "attached client exited early: $stderr"
	}
	$AttachedProcess.StandardInput.Write("echo TMUX_WIN32_ATTACH_MODE`r")
	$AttachedProcess.StandardInput.Flush()
	$capture = Wait-PaneContains "attached client input" "attachsmoke:0.0" `
	    "TMUX_WIN32_ATTACH_MODE" 12000
	Assert-Contains "attached client input" $capture `
	    "TMUX_WIN32_ATTACH_MODE"
	$clients = (Invoke-SmokeTmux @("list-clients", "-F",
	    "#{client_name}:#{client_session}:#{client_control_mode}")).Out
	$attachedClient = ($clients -split "`r?`n" | Where-Object {
	    $_ -like "*:attachsmoke:0"
	} | Select-Object -First 1)
	if ([string]::IsNullOrEmpty($attachedClient)) {
		throw "attached client not found"
	}
	$attachedClientName = ($attachedClient -split ":", 3)[0]
	$popupFile = Join-Path $Temp "display-popup.txt"
	$popupTarget = $popupFile.Replace('\', '/')
	Invoke-SmokeTmux @("display-popup", "-t", $attachedClientName,
	    "-E", "cmd /c echo TMUX_WIN32_POPUP>$popupTarget") 20 |
	    Out-Null
	$popupWait = [Diagnostics.Stopwatch]::StartNew()
	while ($popupWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $popupFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "display-popup" $popupFile "TMUX_WIN32_POPUP"
	Write-Pass "display-popup"
	$popupCwd = Join-Path $Temp "popup-cwd"
	New-Item -ItemType Directory -Force -Path $popupCwd | Out-Null
	$popupCwdFile = Join-Path $popupCwd "display-popup-cwd.txt"
	Invoke-SmokeTmux @("display-popup", "-t", $attachedClientName,
	    "-d", $popupCwd, "-E",
	    "cmd /c echo TMUX_WIN32_POPUP_CWD>display-popup-cwd.txt") 20 |
	    Out-Null
	$popupCwdWait = [Diagnostics.Stopwatch]::StartNew()
	while ($popupCwdWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $popupCwdFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "display-popup cwd" $popupCwdFile `
	    "TMUX_WIN32_POPUP_CWD"
	$popupCwdWithSpaces = Join-Path $Temp "popup cwd with spaces (amp&one)"
	New-Item -ItemType Directory -Force -Path $popupCwdWithSpaces |
	    Out-Null
	$popupCwdWithSpacesFile = Join-Path $popupCwdWithSpaces `
	    "display-popup-cwd-spaces.txt"
	Invoke-SmokeTmux @("display-popup", "-t", $attachedClientName,
	    "-d", $popupCwdWithSpaces, "-E",
	    "cmd /c echo TMUX_WIN32_POPUP_CWD_SPACES>display-popup-cwd-spaces.txt") 20 |
	    Out-Null
	$popupCwdWithSpacesWait = [Diagnostics.Stopwatch]::StartNew()
	while ($popupCwdWithSpacesWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $popupCwdWithSpacesFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "display-popup cwd with spaces" `
	    $popupCwdWithSpacesFile "TMUX_WIN32_POPUP_CWD_SPACES"
	$missingPopupCwd = Join-Path $Temp "missing-popup-cwd"
	$badPopupCwdFile = Join-Path $Temp "display-popup-bad-cwd.txt"
	$badPopupCwdTarget = $badPopupCwdFile.Replace('\', '/')
	Invoke-SmokeTmux @("display-popup", "-t", $attachedClientName,
	    "-d", $missingPopupCwd, "-E",
	    "cmd /c echo TMUX_WIN32_POPUP_BAD_CWD>$badPopupCwdTarget") 20 |
	    Out-Null
	$badPopupCwdWait = [Diagnostics.Stopwatch]::StartNew()
	while ($badPopupCwdWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $badPopupCwdFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "display-popup invalid cwd" $badPopupCwdFile `
	    "TMUX_WIN32_POPUP_BAD_CWD"
	Write-Pass "display-popup cwd"
	$menuCommand = "set-environment -g TMUX_WIN32_MENU yes"
	$menuEnvironment = ""
	for ($menuAttempt = 0; $menuAttempt -lt 3 -and
	    $menuEnvironment -notlike "*TMUX_WIN32_MENU=yes*"; $menuAttempt++) {
		$script:MenuProcess = Start-SmokeTmuxProcess @("display-menu",
		    "-c", $attachedClientName, "-t", "attachsmoke:0.0",
		    "-x", "C", "-y", "C", "-C", "0", "Run menu smoke", "r",
		    $menuCommand)
		$menuReadySw = [Diagnostics.Stopwatch]::StartNew()
		while ($menuReadySw.ElapsedMilliseconds -lt 12000) {
			if ($MenuProcess.HasExited) { break }
			$inMode = (Invoke-SmokeTmux @("display-message", "-p",
			    "-t", "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
			if ($inMode -eq "1") { break }
			Start-Sleep -Milliseconds 100
		}
		if ($MenuProcess.HasExited) {
			$stderr = $MenuProcess.StandardError.ReadToEnd()
			$stdout = $MenuProcess.StandardOutput.ReadToEnd()
			throw "display-menu exited early: $stdout $stderr"
		}
		$AttachedProcess.StandardInput.Write("`r")
		$AttachedProcess.StandardInput.Flush()
		if (-not $MenuProcess.WaitForExit(10000)) {
			throw "display-menu did not close after shortcut"
		}
		if ($MenuProcess.ExitCode -ne 0) {
			$stderr = $MenuProcess.StandardError.ReadToEnd()
			$stdout = $MenuProcess.StandardOutput.ReadToEnd()
			throw ("display-menu exited with " +
			    "$($MenuProcess.ExitCode): $stdout $stderr")
		}
		$menuEnvironment = (Invoke-SmokeTmux @("show-environment",
		    "-g")).Out
	}
	Assert-Contains "display-menu" $menuEnvironment "TMUX_WIN32_MENU=yes"
	Invoke-SmokeTmux @("set-environment", "-gu", "TMUX_WIN32_MENU") |
	    Out-Null
	Write-Pass "display-menu"
	Invoke-SmokeTmux @("set-buffer", "-b", "winchoose",
	    "echo TMUX_WIN32_CHOOSE_BUFFER") | Out-Null
	Invoke-SmokeTmux @("choose-buffer", "-t", "attachsmoke:0.0",
	    "-f", "#{==:#{buffer_name},winchoose}") | Out-Null
	$chooseModePollSw = [Diagnostics.Stopwatch]::StartNew()
	$chooseMode = "0"
	while ($chooseModePollSw.ElapsedMilliseconds -lt 7000) {
		$chooseMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
		    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($chooseMode -eq "1") { break }
		Start-Sleep -Milliseconds 100
	}
	$chooseMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
	    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
	if ($chooseMode -ne "1") {
		throw "choose-buffer did not enter mode: $chooseMode"
	}
	$AttachedProcess.StandardInput.Write("`r")
	$AttachedProcess.StandardInput.Flush()
	$chooseWait = [Diagnostics.Stopwatch]::StartNew()
	while ($chooseWait.ElapsedMilliseconds -lt 5000) {
		Start-Sleep -Milliseconds 100
		$chooseMode = (Invoke-SmokeTmux @("display-message", "-p",
		    "-t", "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($chooseMode -ne "1") {
			break
		}
	}
	if ($chooseMode -eq "1") {
		throw "choose-buffer did not exit mode"
	}
	$AttachedProcess.StandardInput.Write("`r")
	$AttachedProcess.StandardInput.Flush()
	$capture = Wait-PaneContains "choose-buffer result" "attachsmoke:0.0" `
	    "TMUX_WIN32_CHOOSE_BUFFER" 12000
	Assert-Contains "choose-buffer" $capture "TMUX_WIN32_CHOOSE_BUFFER"
	Write-Pass "choose-buffer"
	$treeFile = Join-Path $Temp "choose-tree.txt"
	$treeTarget = $treeFile.Replace('\', '/')
	$treeCommand = "run-shell `"cmd /c echo TMUX_WIN32_CHOOSE_TREE>$treeTarget`""
	Invoke-SmokeTmux @("choose-tree", "-t", "attachsmoke:0.0",
	    "-f", "#{==:#{session_name},attachsmoke}", $treeCommand) |
	    Out-Null
	$treeModePollSw = [Diagnostics.Stopwatch]::StartNew()
	$treeMode = "0"
	while ($treeModePollSw.ElapsedMilliseconds -lt 7000) {
		$treeMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
		    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($treeMode -eq "1") { break }
		Start-Sleep -Milliseconds 100
	}
	$treeMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
	    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
	if ($treeMode -ne "1") {
		throw "choose-tree did not enter mode: $treeMode"
	}
	$AttachedProcess.StandardInput.Write("`r")
	$AttachedProcess.StandardInput.Flush()
	Wait-FileContains "choose-tree" $treeFile "TMUX_WIN32_CHOOSE_TREE"
	$treeModeWait = [Diagnostics.Stopwatch]::StartNew()
	while ($treeModeWait.ElapsedMilliseconds -lt 5000) {
		Start-Sleep -Milliseconds 100
		$treeMode = (Invoke-SmokeTmux @("display-message", "-p",
		    "-t", "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($treeMode -ne "1") {
			break
		}
	}
	if ($treeMode -eq "1") {
		throw "choose-tree did not exit mode"
	}
	Write-Pass "choose-tree"
	$promptName = "TMUX_WIN32_COMMAND_PROMPT"
	$promptCommand = "set-environment -g $promptName %1"
	Invoke-SmokeTmux @("command-prompt", "-b", "-t",
	    $attachedClientName, "-p", "prompt smoke", "-I",
	    "TMUX_WIN32_COMMAND_PROMPT", $promptCommand) |
	    Out-Null
	$promptReadySw = [Diagnostics.Stopwatch]::StartNew()
	while ($promptReadySw.ElapsedMilliseconds -lt 12000) {
		$inMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
		    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($inMode -eq "1") { break }
		Start-Sleep -Milliseconds 100
	}
	$promptWait = [Diagnostics.Stopwatch]::StartNew()
	while ($promptWait.ElapsedMilliseconds -lt 10000) {
		$AttachedProcess.StandardInput.Write("`r")
		$AttachedProcess.StandardInput.Flush()
		Start-Sleep -Milliseconds 300
		$promptEnvironment = (Invoke-SmokeTmux @("show-environment",
		    "-g")).Out
		if ($promptEnvironment -like "*$promptName=*") {
			break
		}
	}
	Assert-Contains "command-prompt" $promptEnvironment `
	    "$promptName=TMUX_WIN32_COMMAND_PROMPT"
	Invoke-SmokeTmux @("set-environment", "-gu", $promptName) |
	    Out-Null
	Write-Pass "command-prompt"
	$confirmName = "TMUX_WIN32_CONFIRM_BEFORE"
	$confirmCommand = "set-environment -g $confirmName yes"
	Invoke-SmokeTmux @("confirm-before", "-b", "-t",
	    $attachedClientName, "-p", "confirm smoke?", $confirmCommand) |
	    Out-Null
	$confirmReadySw = [Diagnostics.Stopwatch]::StartNew()
	while ($confirmReadySw.ElapsedMilliseconds -lt 12000) {
		$inMode = (Invoke-SmokeTmux @("display-message", "-p", "-t",
		    "attachsmoke:0.0", "#{pane_in_mode}")).Out.Trim()
		if ($inMode -eq "1") { break }
		Start-Sleep -Milliseconds 100
	}
	$confirmWait = [Diagnostics.Stopwatch]::StartNew()
	while ($confirmWait.ElapsedMilliseconds -lt 10000) {
		$AttachedProcess.StandardInput.Write("y")
		$AttachedProcess.StandardInput.Flush()
		Start-Sleep -Milliseconds 300
		$confirmEnvironment = (Invoke-SmokeTmux @("show-environment",
		    "-g")).Out
		if ($confirmEnvironment -like "*$confirmName=*") {
			break
		}
	}
	Assert-Contains "confirm-before" $confirmEnvironment `
	    "$confirmName=yes"
	Invoke-SmokeTmux @("set-environment", "-gu", $confirmName) |
	    Out-Null
	Write-Pass "confirm-before"
	if (Initialize-SystemClipboard) {
		$savedClipboard = Save-SystemClipboard
		try {
			$clipboardSetText = "TMUX_WIN32_CLIPBOARD_SET_$ServerName"
			Invoke-SmokeTmux @("set-buffer", "-w", "-t",
			    $attachedClientName, "-b", "winclipset",
			    $clipboardSetText) | Out-Null
			Wait-SystemClipboardText "set-buffer -w" `
			    $clipboardSetText | Out-Null
			$clipboardGetText = "TMUX_WIN32_CLIPBOARD_GET_$ServerName"
			Set-SystemClipboardText $clipboardGetText
			Wait-SystemClipboardText "prepare refresh-client -l" `
			    $clipboardGetText 10000 | Out-Null
			Invoke-SmokeTmux @("refresh-client", "-l", "-t",
			    $attachedClientName) | Out-Null
			$clipboardBuffer = ""
			$clipboardLoadWait = [Diagnostics.Stopwatch]::StartNew()
			while ($clipboardLoadWait.ElapsedMilliseconds -lt 10000) {
				$clipboardBuffer = (Invoke-SmokeTmux @(
				    "show-buffer")).Out
				if ($clipboardBuffer -like "*$clipboardGetText*") {
					break
				}
				Start-Sleep -Milliseconds 200
			}
			Assert-Contains "refresh-client -l" $clipboardBuffer `
			    $clipboardGetText
			Invoke-SmokeTmux @("set-option", "-s",
			    "set-clipboard", "on") | Out-Null
			$osc52Text = "TMUX_WIN32_OSC52_$ServerName"
			$osc52Base64 = [Convert]::ToBase64String(
			    [Text.Encoding]::UTF8.GetBytes($osc52Text))
			$osc52Payload = "[Console]::Write([char]27 + " +
			    "']52;c;$osc52Base64' + [char]7)"
			$osc52Command = "powershell -NoProfile -NonInteractive " +
			    "-Command " +
			    "`"$osc52Payload`""
			Invoke-SmokeTmux @("send-keys", "-t",
			    "attachsmoke:0.0", $osc52Command, "Enter") |
			    Out-Null
			Wait-SystemClipboardText "OSC 52 pane clipboard" `
			    $osc52Text 15000 | Out-Null
			$osc52Buffer = (Invoke-SmokeTmux @("show-buffer")).Out
			Assert-Contains "OSC 52 pane clipboard buffer" `
			    $osc52Buffer $osc52Text
			Write-Pass "Windows clipboard"
		} finally {
			Restore-SystemClipboard $savedClipboard
		}
	} else {
		Write-Host "[SKIP] Windows clipboard unavailable"
	}
	Invoke-SmokeTmux @("detach-client", "-t", $attachedClientName) |
	    Out-Null
	if (-not $AttachedProcess.WaitForExit(5000)) {
		throw "attached client did not detach"
	}
	if ($AttachedProcess.ExitCode -ne 0) {
		throw "attached client exited with $($AttachedProcess.ExitCode)"
	}

	$consoleAttachedName = "TMUX_WIN32_CONSOLE_ATTACHED"
	$consoleDetachedName = "TMUX_WIN32_CONSOLE_DETACHED"
	$consoleAttachExit = Join-Path $Temp "console-attach-exit.txt"
	$consoleAttachHelper = Join-Path $Temp "console-attach-helper.ps1"
	$helperTmux = $Tmux.Replace("'", "''")
	$helperServer = $ServerName.Replace("'", "''")
	$helperExit = $consoleAttachExit.Replace("'", "''")
	Set-Content -LiteralPath $consoleAttachHelper -Encoding ascii -Value @"
& '$helperTmux' -L '$helperServer' -f NUL attach -t attachsmoke
Set-Content -LiteralPath '$helperExit' -Value `$LASTEXITCODE
"@
	Invoke-SmokeTmux @("set-environment", "-gu",
	    $consoleAttachedName) | Out-Null
	Invoke-SmokeTmux @("set-environment", "-gu",
	    $consoleDetachedName) | Out-Null
	Invoke-SmokeTmux @("set-hook", "-g", "client-attached",
	    "set-environment -g $consoleAttachedName yes") | Out-Null
	Invoke-SmokeTmux @("set-hook", "-g", "client-detached",
	    "set-environment -g $consoleDetachedName yes") | Out-Null
	$consoleAttachProcess = Start-SmokePowerShellProcess @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
	    $consoleAttachHelper)
	$consoleEnvironment = ""
	$consoleAttachWait = [Diagnostics.Stopwatch]::StartNew()
	while ($consoleAttachWait.ElapsedMilliseconds -lt 10000) {
		$consoleEnvironment = (Invoke-SmokeTmux @(
		    "show-environment", "-g")).Out
		if ($consoleEnvironment -like "*$consoleAttachedName=yes*") {
			break
		}
		Start-Sleep -Milliseconds 200
	}
	Assert-Contains "console attach hook" $consoleEnvironment `
	    "$consoleAttachedName=yes"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $consoleAttachProcess.WaitForExit(10000)) {
		$consoleAttachProcess.Kill()
		throw "console attach helper did not exit"
	}
	$consoleDetachWait = [Diagnostics.Stopwatch]::StartNew()
	while ($consoleDetachWait.ElapsedMilliseconds -lt 10000) {
		$consoleEnvironment = (Invoke-SmokeTmux @(
		    "show-environment", "-g")).Out
		if ($consoleEnvironment -like "*$consoleDetachedName=yes*") {
			break
		}
		Start-Sleep -Milliseconds 200
	}
	Assert-Contains "console detach hook" $consoleEnvironment `
	    "$consoleDetachedName=yes"
	Write-Pass "console attach hooks"

	$realConsoleMarker = "TMUX_WIN32_REAL_CONSOLE_ATTACH"
	$realConsoleStarted = Join-Path $Temp "real-console-started.txt"
	$realConsoleInput = Join-Path $Temp "real-console-input.txt"
	$realConsoleExit = Join-Path $Temp "real-console-exit.txt"
	$realConsoleSize = Join-Path $Temp "real-console-size.txt"
	$realConsoleResized = Join-Path $Temp "real-console-resized.txt"
	$realConsoleResizeLog = Join-Path $Temp "real-console-resize-log.txt"
	$realConsoleResizeMarker = "TMUX_WIN32_REAL_CONSOLE_RESIZE"
	$realConsoleResizeChurnMarker = "TMUX_WIN32_REAL_CONSOLE_CHURN_"
	$realConsoleProbe = Join-Path $PSScriptRoot "console-attach-probe.ps1"
	$realConsoleProcess = Start-SmokePowerShellProcess @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $realConsoleProbe, "-Tmux", $Tmux, "-ServerName",
	    $ServerName, "-Session", "attachsmoke", "-Marker",
	    $realConsoleMarker, "-StartedFile", $realConsoleStarted,
	    "-InputFile", $realConsoleInput, "-ExitFile", $realConsoleExit,
	    "-SizeFile", $realConsoleSize, "-ResizeWidth", "88",
	    "-ResizeHeight", "26", "-ResizedFile", $realConsoleResized,
	    "-ResizeMarker", $realConsoleResizeMarker,
	    "-ResizeSequence", "94x28,86x25,90x27",
	    "-ResizeLogFile", $realConsoleResizeLog,
	    "-ResizeMarkerPrefix", $realConsoleResizeChurnMarker)
	$realConsoleWait = [Diagnostics.Stopwatch]::StartNew()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleInput)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console attach input sent" `
	    $realConsoleInput "sent"
	$realConsoleCapture = ""
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like "*$realConsoleMarker*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console attach pane input" `
	    $realConsoleCapture $realConsoleMarker
	$realConsoleClients = (Invoke-SmokeTmux @("list-clients", "-F",
	    "#{client_session}:#{client_width}x#{client_height}")).Out
	Assert-Contains "real console attach client" `
	    $realConsoleClients "attachsmoke:"
	Assert-FileContains "real console attach size" `
	    $realConsoleSize "x"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleResized)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console attach resized" `
	    $realConsoleResized "x"
	$realConsoleResizeSize = (Get-Content -LiteralPath `
	    $realConsoleResized -Raw).Trim()
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleClients = (Invoke-SmokeTmux @("list-clients",
		    "-F", "#{client_session}:#{client_width}x#{client_height}"
		    )).Out
		if ($realConsoleClients -like
		    "*attachsmoke:$realConsoleResizeSize*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console attach resize" `
	    $realConsoleClients "attachsmoke:$realConsoleResizeSize"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like "*$realConsoleResizeMarker*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console attach input after resize" `
	    $realConsoleCapture $realConsoleResizeMarker
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 20000 -and
	    -not (Test-Path -LiteralPath $realConsoleResizeLog)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console attach resize churn log" `
	    $realConsoleResizeLog "$($realConsoleResizeChurnMarker)0"
	Assert-FileContains "real console attach resize churn log" `
	    $realConsoleResizeLog "$($realConsoleResizeChurnMarker)1"
	Assert-FileContains "real console attach resize churn log" `
	    $realConsoleResizeLog "$($realConsoleResizeChurnMarker)2"
	$realConsoleResizeLines = @(Get-Content -LiteralPath `
	    $realConsoleResizeLog)
	$realConsoleLastResize = $realConsoleResizeLines[-1]
	if ($realConsoleLastResize -notmatch "^[0-9]+:([^:]+):") {
		throw "real console attach invalid resize log: $realConsoleLastResize"
	}
	$realConsoleLastSize = $Matches[1]
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleClients = (Invoke-SmokeTmux @("list-clients",
		    "-F", "#{client_session}:#{client_width}x#{client_height}"
		    )).Out
		if ($realConsoleClients -like
		    "*attachsmoke:$realConsoleLastSize*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console attach resize churn final size" `
	    $realConsoleClients "attachsmoke:$realConsoleLastSize"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like
		    "*$($realConsoleResizeChurnMarker)2*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console attach input after resize churn" `
	    $realConsoleCapture "$($realConsoleResizeChurnMarker)2"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $realConsoleProcess.WaitForExit(10000)) {
		$realConsoleProcess.Kill()
		throw "real console attach probe did not exit"
	}
	if ($realConsoleProcess.ExitCode -ne 0) {
		throw ("real console attach probe exited with {0}" -f `
		    $realConsoleProcess.ExitCode)
	}
	Assert-FileContains "real console attach exit" `
	    $realConsoleExit "0"
	Write-Pass "real console attach input/repeated-resize"

	$realConsoleCtrlCMarker = "TMUX_WIN32_REAL_CONSOLE_CTRL_C"
	$realConsoleCtrlCStarted = Join-Path $Temp `
	    "real-console-ctrlc-started.txt"
	$realConsoleCtrlCInput = Join-Path $Temp `
	    "real-console-ctrlc-input.txt"
	$realConsoleCtrlCExit = Join-Path $Temp `
	    "real-console-ctrlc-exit.txt"
	$realConsoleCtrlCSize = Join-Path $Temp `
	    "real-console-ctrlc-size.txt"
	$realConsoleCtrlCFile = Join-Path $Temp `
	    "real-console-ctrlc-sent.txt"
	$realConsoleCtrlCProcess = Start-SmokePowerShellProcess @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $realConsoleProbe, "-Tmux", $Tmux, "-ServerName",
	    $ServerName, "-Session", "attachsmoke", "-Marker",
	    "TMUX_WIN32_REAL_CONSOLE_CTRL_C_ATTACH", "-StartedFile",
	    $realConsoleCtrlCStarted, "-InputFile", $realConsoleCtrlCInput,
	    "-ExitFile", $realConsoleCtrlCExit, "-SizeFile",
	    $realConsoleCtrlCSize, "-CtrlCCommand",
	    "timeout /t 30 /nobreak", "-CtrlCFile",
	    $realConsoleCtrlCFile, "-CtrlCMarker", $realConsoleCtrlCMarker)
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleCtrlCFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console Ctrl+C sent" `
	    $realConsoleCtrlCFile "sent"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like "*$realConsoleCtrlCMarker*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console Ctrl+C interrupt" `
	    $realConsoleCapture $realConsoleCtrlCMarker
	$realConsoleCommand = Wait-PaneCurrentCommand `
	    "real console Ctrl+C shell restored" "attachsmoke:0.0" `
	    "cmd.exe" 12000
	Assert-Contains "real console Ctrl+C shell restored" `
	    $realConsoleCommand "cmd.exe"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $realConsoleCtrlCProcess.WaitForExit(10000)) {
		$realConsoleCtrlCProcess.Kill()
		throw "real console Ctrl+C probe did not exit"
	}
	if ($realConsoleCtrlCProcess.ExitCode -ne 0) {
		throw ("real console Ctrl+C probe exited with {0}" -f `
		    $realConsoleCtrlCProcess.ExitCode)
	}
	Assert-FileContains "real console Ctrl+C exit" `
	    $realConsoleCtrlCExit "0"
	Write-Pass "real console attach Ctrl+C"

	$realConsolePowerShellCtrlCMarker =
	    "TMUX_WIN32_REAL_CONSOLE_POWERSHELL_CTRL_C"
	$realConsolePowerShellCtrlCStarted = Join-Path $Temp `
	    "real-console-powershell-ctrlc-started.txt"
	$realConsolePowerShellCtrlCInput = Join-Path $Temp `
	    "real-console-powershell-ctrlc-input.txt"
	$realConsolePowerShellCtrlCExit = Join-Path $Temp `
	    "real-console-powershell-ctrlc-exit.txt"
	$realConsolePowerShellCtrlCSize = Join-Path $Temp `
	    "real-console-powershell-ctrlc-size.txt"
	$realConsolePowerShellCtrlCFile = Join-Path $Temp `
	    "real-console-powershell-ctrlc-sent.txt"
	$realConsolePowerShellCtrlCCommand =
	    '"powershell -NoProfile -Command ""Start-Sleep -Seconds 30"""'
	$realConsolePowerShellCtrlCArguments = @("-NoProfile",
	    "-ExecutionPolicy", "Bypass", "-File", $realConsoleProbe,
	    "-Tmux", $Tmux, "-ServerName", $ServerName, "-Session",
	    "attachsmoke", "-Marker",
	    "TMUX_WIN32_REAL_CONSOLE_POWERSHELL_CTRL_C_ATTACH",
	    "-StartedFile", $realConsolePowerShellCtrlCStarted,
	    "-InputFile", $realConsolePowerShellCtrlCInput, "-ExitFile",
	    $realConsolePowerShellCtrlCExit, "-SizeFile",
	    $realConsolePowerShellCtrlCSize, "-CtrlCCommand",
	    $realConsolePowerShellCtrlCCommand, "-CtrlCFile",
	    $realConsolePowerShellCtrlCFile, "-CtrlCMarker",
	    $realConsolePowerShellCtrlCMarker)
	$realConsolePowerShellCtrlCStart =
	    [System.Diagnostics.ProcessStartInfo]::new()
	$realConsolePowerShellCtrlCStart.FileName = "powershell.exe"
	$realConsolePowerShellCtrlCStart.Arguments =
	    ($realConsolePowerShellCtrlCArguments | ForEach-Object {
		ConvertTo-WindowsArgument $_
	    }) -join " "
	$realConsolePowerShellCtrlCStart.UseShellExecute = $false
	$realConsolePowerShellCtrlCStart.CreateNoWindow = $true
	$realConsolePowerShellCtrlCProcess =
	    [System.Diagnostics.Process]::Start(
	    $realConsolePowerShellCtrlCStart)
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsolePowerShellCtrlCFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console PowerShell Ctrl+C sent" `
	    $realConsolePowerShellCtrlCFile "sent"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 15000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like
		    "*$realConsolePowerShellCtrlCMarker*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console PowerShell Ctrl+C interrupt" `
	    $realConsoleCapture $realConsolePowerShellCtrlCMarker
	$realConsoleCommand = Wait-PaneCurrentCommand `
	    "real console PowerShell Ctrl+C shell restored" `
	    "attachsmoke:0.0" "cmd.exe" 12000
	Assert-Contains "real console PowerShell Ctrl+C shell restored" `
	    $realConsoleCommand "cmd.exe"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $realConsolePowerShellCtrlCProcess.WaitForExit(10000)) {
		$realConsolePowerShellCtrlCProcess.Kill()
		throw "real console PowerShell Ctrl+C probe did not exit"
	}
	if ($realConsolePowerShellCtrlCProcess.ExitCode -ne 0) {
		throw ("real console PowerShell Ctrl+C probe exited with {0}" -f `
		    $realConsolePowerShellCtrlCProcess.ExitCode)
	}
	Assert-FileContains "real console PowerShell Ctrl+C exit" `
	    $realConsolePowerShellCtrlCExit "0"
	Write-Pass "real console attach PowerShell Ctrl+C"

	$realConsoleCtrlBreakMarker = "TMUX_WIN32_REAL_CONSOLE_CTRL_BREAK"
	$realConsoleCtrlBreakStarted = Join-Path $Temp `
	    "real-console-ctrlbreak-started.txt"
	$realConsoleCtrlBreakInput = Join-Path $Temp `
	    "real-console-ctrlbreak-input.txt"
	$realConsoleCtrlBreakExit = Join-Path $Temp `
	    "real-console-ctrlbreak-exit.txt"
	$realConsoleCtrlBreakSize = Join-Path $Temp `
	    "real-console-ctrlbreak-size.txt"
	$realConsoleCtrlBreakFile = Join-Path $Temp `
	    "real-console-ctrlbreak-sent.txt"
	$realConsoleCtrlBreakProcess = Start-SmokePowerShellProcess @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $realConsoleProbe, "-Tmux", $Tmux, "-ServerName",
	    $ServerName, "-Session", "attachsmoke", "-Marker",
	    "TMUX_WIN32_REAL_CONSOLE_CTRL_BREAK_ATTACH", "-StartedFile",
	    $realConsoleCtrlBreakStarted, "-InputFile",
	    $realConsoleCtrlBreakInput, "-ExitFile",
	    $realConsoleCtrlBreakExit, "-SizeFile",
	    $realConsoleCtrlBreakSize, "-CtrlBreakCommand",
	    "timeout /t 30 /nobreak", "-CtrlBreakFile",
	    $realConsoleCtrlBreakFile, "-CtrlBreakMarker",
	    $realConsoleCtrlBreakMarker)
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleCtrlBreakFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console Ctrl+Break sent" `
	    $realConsoleCtrlBreakFile "sent"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000) {
		$realConsoleCapture = (Invoke-SmokeTmux @(
		    "capture-pane", "-p", "-t", "attachsmoke:0.0")).Out
		if ($realConsoleCapture -like
		    "*$realConsoleCtrlBreakMarker*") {
			break
		}
		Start-Sleep -Milliseconds 300
	}
	Assert-Contains "real console Ctrl+Break interrupt" `
	    $realConsoleCapture $realConsoleCtrlBreakMarker
	$realConsoleCommand = Wait-PaneCurrentCommand `
	    "real console Ctrl+Break shell restored" "attachsmoke:0.0" `
	    "cmd.exe" 12000
	Assert-Contains "real console Ctrl+Break shell restored" `
	    $realConsoleCommand "cmd.exe"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $realConsoleCtrlBreakProcess.WaitForExit(10000)) {
		$realConsoleCtrlBreakProcess.Kill()
		throw "real console Ctrl+Break probe did not exit"
	}
	if ($realConsoleCtrlBreakProcess.ExitCode -ne 0) {
		throw ("real console Ctrl+Break probe exited with {0}" -f `
		    $realConsoleCtrlBreakProcess.ExitCode)
	}
	Assert-FileContains "real console Ctrl+Break exit" `
	    $realConsoleCtrlBreakExit "0"
	Write-Pass "real console attach Ctrl+Break"

	$realConsoleRawReady = Join-Path $Temp `
	    "real-console-raw-ctrlc-ready.txt"
	$realConsoleRawFile = Join-Path $Temp `
	    "real-console-raw-ctrlc-etx.txt"
	$realConsoleRawStarted = Join-Path $Temp `
	    "real-console-raw-ctrlc-started.txt"
	$realConsoleRawInput = Join-Path $Temp `
	    "real-console-raw-ctrlc-input.txt"
	$realConsoleRawExit = Join-Path $Temp `
	    "real-console-raw-ctrlc-exit.txt"
	$realConsoleRawSize = Join-Path $Temp `
	    "real-console-raw-ctrlc-size.txt"
	$realConsoleRawCtrlCFile = Join-Path $Temp `
	    "real-console-raw-ctrlc-sent.txt"
	$realConsoleRawCommand = "powershell -NoProfile -NonInteractive " +
	    "-ExecutionPolicy Bypass -File `"$etxScript`" " +
	    "-Ready `"$realConsoleRawReady`" " +
	    "-Output `"$realConsoleRawFile`""
	Invoke-SmokeTmux @("send-keys", "-t", "attachsmoke:0.0",
	    $realConsoleRawCommand, "Enter") | Out-Null
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 10000 -and
	    -not (Test-Path -LiteralPath $realConsoleRawReady)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console raw Ctrl+C ready" `
	    $realConsoleRawReady "ready"
	$realConsoleRawProcess = Start-SmokePowerShellProcess @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass",
	    "-File", $realConsoleProbe, "-Tmux", $Tmux, "-ServerName",
	    $ServerName, "-Session", "attachsmoke", "-Marker",
	    "TMUX_WIN32_REAL_CONSOLE_RAW_CTRL_C_ATTACH", "-StartedFile",
	    $realConsoleRawStarted, "-InputFile", $realConsoleRawInput,
	    "-ExitFile", $realConsoleRawExit, "-SizeFile",
	    $realConsoleRawSize, "-SkipInitialInput", "-CtrlCFile",
	    $realConsoleRawCtrlCFile)
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleRawCtrlCFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console raw Ctrl+C sent" `
	    $realConsoleRawCtrlCFile "sent"
	$realConsoleWait.Restart()
	while ($realConsoleWait.ElapsedMilliseconds -lt 12000 -and
	    -not (Test-Path -LiteralPath $realConsoleRawFile)) {
		Start-Sleep -Milliseconds 200
	}
	Assert-FileContains "real console raw Ctrl+C ETX" `
	    $realConsoleRawFile "TMUX_WIN32_ETX_BYTE"
	try {
		Invoke-SmokeTmux @("detach-client", "-s", "attachsmoke") 5 |
		    Out-Null
	} catch {
	}
	if (-not $realConsoleRawProcess.WaitForExit(10000)) {
		$realConsoleRawProcess.Kill()
		throw "real console raw Ctrl+C probe did not exit"
	}
	if ($realConsoleRawProcess.ExitCode -ne 0) {
		throw ("real console raw Ctrl+C probe exited with {0}" -f `
		    $realConsoleRawProcess.ExitCode)
	}
	Assert-FileContains "real console raw Ctrl+C exit" `
	    $realConsoleRawExit "0"
	Write-Pass "real console attach raw Ctrl+C"

	Invoke-SmokeTmux @("kill-session", "-t", "attachsmoke") 120 |
	    Out-Null
	Write-Pass "attached client"

	Stop-SmokeServer
	Write-Pass "kill-server"
	Write-Host "Windows runtime smoke passed."
} finally {
	Stop-MenuProcess
	Stop-AttachedClient
	Stop-ControlClient
	try {
		Invoke-SmokeTmux @("kill-server") 3 | Out-Null
	} catch {
	}
	Stop-SmokeProcesses
	if (Test-Path -LiteralPath $Endpoint) {
		Remove-Item -LiteralPath $Endpoint -Force
	}
	Remove-SmokeTemp
	if ($null -eq $OldParseDir) {
		Remove-Item -Path "env:TMUX_WIN32_PARSE_DIR" `
		    -ErrorAction SilentlyContinue
	} else {
		Set-Item -Path "env:TMUX_WIN32_PARSE_DIR" -Value $OldParseDir
	}
}
