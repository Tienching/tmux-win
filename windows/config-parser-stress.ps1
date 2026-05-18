param(
	[string]$Tmux = "",
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

$ServerName = "codex-config-stress-" + [Guid]::NewGuid().ToString("N")
$Temp = Join-Path ([System.IO.Path]::GetTempPath()) $ServerName
$Endpoint = Join-Path (Join-Path $env:LOCALAPPDATA "tmux") `
    ($ServerName + ".endpoint")
$OldConfigDir = [Environment]::GetEnvironmentVariable(
    "TMUX_WIN32_CONFIG_STRESS_DIR", "Process")

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

function Invoke-ConfigTmux([string[]]$Arguments,
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
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	if (-not $process.WaitForExit($Timeout * 1000)) {
		try {
			$process.Kill()
		} catch {
		}
		throw "tmux timed out: $($Arguments -join ' ')"
	}
	$process.WaitForExit()

	$stdout = $stdoutTask.Result
	$stderr = $stderrTask.Result
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

function Wait-FileContains([string]$Name, [string]$Path, [string]$Needle,
    [int]$Timeout = $TimeoutSeconds) {
	$sw = [Diagnostics.Stopwatch]::StartNew()
	while ($sw.Elapsed.TotalSeconds -lt $Timeout) {
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
	if (-not (Test-Path -LiteralPath $Path)) {
		throw "$Name did not create file: $Path"
	}
	$content = Get-Content -LiteralPath $Path -Raw
	Assert-Contains $Name $content $Needle
}

function Assert-Environment([string]$Name, [string]$Needle) {
	$value = (Invoke-ConfigTmux @("show-environment", "-g", $Name)).Out
	Assert-Contains $Name $value $Needle
}

function Stop-ConfigServer {
	try {
		Invoke-ConfigTmux @("kill-server") 5 | Out-Null
	} catch {
	}
	Start-Sleep -Milliseconds 300
	Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object {
		    $_.CommandLine -and $_.CommandLine.Contains($ServerName)
	    } | ForEach-Object {
		    try {
			    Stop-Process -Id $_.ProcessId -Force
		    } catch {
		    }
	    }
}

New-Item -ItemType Directory -Force -Path $Temp | Out-Null

try {
	$configDir = Join-Path $Temp "config path with spaces (amp&one)"
	$nestedDir = Join-Path $configDir "nested configs"
	New-Item -ItemType Directory -Force -Path $nestedDir | Out-Null
	Set-Item -Path "env:TMUX_WIN32_CONFIG_STRESS_DIR" -Value $configDir

	$hookFile = Join-Path $Temp "hook.txt"
	$ifShellFile = Join-Path $Temp "if-shell.txt"
	$hookTarget = $hookFile.Replace('\', '/')
	$ifShellTarget = $ifShellFile.Replace('\', '/')
	$nestedConfig = Join-Path $nestedDir "nested config.conf"
	$nestedTarget = $nestedConfig.Replace('\', '/')
	$globA = Join-Path $configDir "glob-a.conf"
	$globB = Join-Path $configDir "glob-b.conf"
	$mainConfig = Join-Path $Temp "main config with spaces.conf"

	Set-Content -LiteralPath $nestedConfig -Encoding ascii -Value @(
	    'set-environment -g TMUX_WIN32_CONFIG_NESTED yes',
	    'set-option -g status-left "WIN32CFG #{session_name} C:\\tmux\\path with spaces"',
	    'bind-key -T prefix F12 display-message "TMUX_WIN32_CONFIG_BIND"'
	)
	Set-Content -LiteralPath $globA -Encoding ascii -Value @(
	    'set-environment -g TMUX_WIN32_CONFIG_GLOB_A yes',
	    ('source-file "' + $nestedTarget + '"')
	)
	Set-Content -LiteralPath $globB -Encoding ascii -Value @(
	    'set-environment -g TMUX_WIN32_CONFIG_GLOB_B yes',
	    ('set-hook -g after-new-window "run-shell \"cmd /c echo TMUX_WIN32_CONFIG_HOOK>' + $hookTarget + '\""')
	)
	Set-Content -LiteralPath $mainConfig -Encoding ascii -Value @(
	    'set-environment -g TMUX_WIN32_CONFIG_MAIN yes ; set-environment -g TMUX_WIN32_CONFIG_SEMI yes',
	    'set-option -g status-right "RIGHT #{session_name}:#{window_index}"',
	    ('source-file "' + $nestedTarget + '"'),
	    ('if-shell "cmd /c exit 0" "run-shell \"cmd /c echo TMUX_WIN32_CONFIG_IF>' + $ifShellTarget + '\""')
	)

	Invoke-ConfigTmux @("new-session", "-d", "-s", "cfg", "cmd.exe") |
	    Out-Null
	Invoke-ConfigTmux @("source-file", $mainConfig) | Out-Null
	Invoke-ConfigTmux @("source-file",
	    '%TMUX_WIN32_CONFIG_STRESS_DIR%\glob-*.conf') | Out-Null

	Assert-Environment "TMUX_WIN32_CONFIG_MAIN" "yes"
	Assert-Environment "TMUX_WIN32_CONFIG_SEMI" "yes"
	Assert-Environment "TMUX_WIN32_CONFIG_GLOB_A" "yes"
	Assert-Environment "TMUX_WIN32_CONFIG_GLOB_B" "yes"
	Assert-Environment "TMUX_WIN32_CONFIG_NESTED" "yes"

	$statusLeft = (Invoke-ConfigTmux @("show-option", "-gqv",
	    "status-left")).Out
	Assert-Contains "status-left config format" $statusLeft "WIN32CFG"
	Assert-Contains "status-left Windows path" $statusLeft `
	    "C:\tmux\path with spaces"
	$statusRight = (Invoke-ConfigTmux @("show-option", "-gqv",
	    "status-right")).Out
	Assert-Contains "status-right config format" $statusRight `
	    "RIGHT #{session_name}:#{window_index}"

	$keyList = (Invoke-ConfigTmux @("list-keys", "-T", "prefix")).Out
	Assert-Contains "config bind-key" $keyList "TMUX_WIN32_CONFIG_BIND"

	Invoke-ConfigTmux @("new-window", "-d", "-t", "cfg", "-n",
	    "hook", "cmd.exe") | Out-Null
	Wait-FileContains "config after-new-window hook" $hookFile `
	    "TMUX_WIN32_CONFIG_HOOK"
	Wait-FileContains "config if-shell" $ifShellFile `
	    "TMUX_WIN32_CONFIG_IF"

	Stop-ConfigServer
	Write-Host "Windows config parser stress passed."
} finally {
	Stop-ConfigServer
	if ($null -eq $OldConfigDir) {
		Remove-Item -Path "env:TMUX_WIN32_CONFIG_STRESS_DIR" `
		    -ErrorAction SilentlyContinue
	} else {
		Set-Item -Path "env:TMUX_WIN32_CONFIG_STRESS_DIR" `
		    -Value $OldConfigDir
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
