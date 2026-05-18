param(
	[string]$Tmux = "",
	[string]$SummaryPath = "",
	[int]$TimeoutSeconds = 30
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

if (-not [string]::IsNullOrWhiteSpace($SummaryPath) -and
    -not [System.IO.Path]::IsPathRooted($SummaryPath)) {
	$SummaryPath = Join-Path (Get-Location) $SummaryPath
}
if (-not [string]::IsNullOrWhiteSpace($SummaryPath)) {
	$SummaryPath = [System.IO.Path]::GetFullPath($SummaryPath)
}

$ServerName = "codex-command-surface-" + [Guid]::NewGuid().ToString("N")
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

function Invoke-AuditTmux([string[]]$Arguments,
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

function Split-Lines([string]$Text) {
	return @($Text -split "`r?`n" | Where-Object {
	    -not [string]::IsNullOrWhiteSpace($_)
	})
}

function Get-CommandNames([string[]]$Lines) {
	return @($Lines | ForEach-Object {
	    ($_ -split '[ (]', 2)[0]
	})
}

function Get-OptionNames([string[]]$Lines) {
	return @($Lines | ForEach-Object {
	    if ($_ -match '^([^\s\[]+)') {
		    $Matches[1]
	    }
	})
}

function Require-Items([string]$Name, [string[]]$Actual,
    [string[]]$Required) {
	$set = [System.Collections.Generic.HashSet[string]]::new(
	    [System.StringComparer]::Ordinal)
	foreach ($item in $Actual) {
		[void]$set.Add($item)
	}
	$missing = @($Required | Where-Object { -not $set.Contains($_) })
	if ($missing.Count -gt 0) {
		throw "$Name missing: $($missing -join ', ')"
	}
}

function Require-Minimum([string]$Name, [int]$Actual, [int]$Minimum) {
	if ($Actual -lt $Minimum) {
		throw "$Name too small: expected at least $Minimum, got $Actual"
	}
}

function Require-Equal([string]$Name, [string]$Actual, [string]$Expected) {
	if ($Actual -ne $Expected) {
		throw "$Name mismatch: expected '$Expected', got '$Actual'"
	}
}

function Require-EndsWith([string]$Name, [string]$Actual,
    [string]$ExpectedSuffix) {
	if (-not $Actual.EndsWith($ExpectedSuffix,
	    [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "$Name mismatch: expected suffix '$ExpectedSuffix', got '$Actual'"
	}
}

function Get-GlobalOptionValue([string]$Name) {
	return (Invoke-AuditTmux @("show-options", "-gqv", $Name)).Out.Trim()
}

function Get-ServerOptionValue([string]$Name) {
	return (Invoke-AuditTmux @("show-options", "-sqv", $Name)).Out.Trim()
}

function Get-WindowOptionValue([string]$Name) {
	return (Invoke-AuditTmux @("show-window-options", "-gv", $Name)).Out.
	    Trim()
}

function Get-AuditTmuxProcesses {
	$escaped = $ServerName.Replace("'", "''")
	return @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object { $_.CommandLine -like "*$escaped*" })
}

function Stop-AuditServer {
	try {
		Invoke-AuditTmux @("kill-server") 10 | Out-Null
	} catch {
	}
	Start-Sleep -Milliseconds 500
	Get-AuditTmuxProcesses | ForEach-Object {
		try {
			Stop-Process -Id $_.ProcessId -Force
		} catch {
		}
	}
	if (Test-Path -LiteralPath $Endpoint) {
		Remove-Item -LiteralPath $Endpoint -Force
	}
}

$requiredCommands = @(
    "attach-session", "bind-key", "break-pane", "capture-pane",
    "choose-buffer", "choose-client", "choose-tree", "clear-history",
    "clear-prompt-history", "clock-mode", "command-prompt",
    "confirm-before", "copy-mode", "customize-mode", "delete-buffer",
    "detach-client", "display-menu", "display-message", "display-popup",
    "display-panes", "find-window", "has-session", "if-shell",
    "join-pane", "kill-pane", "kill-server", "kill-session",
    "kill-window", "last-pane", "last-window", "link-window",
    "list-buffers", "list-clients", "list-commands", "list-keys",
    "list-panes", "list-sessions", "list-windows", "load-buffer",
    "lock-client", "lock-server", "lock-session", "move-pane",
    "move-window", "new-session", "new-window", "next-layout",
    "next-window", "paste-buffer", "pipe-pane", "previous-layout",
    "previous-window", "refresh-client", "rename-session",
    "rename-window", "resize-pane", "resize-window", "respawn-pane",
    "respawn-window", "rotate-window", "run-shell", "save-buffer",
    "select-layout", "select-pane", "select-window", "send-keys",
    "send-prefix", "server-access", "set-buffer", "set-environment",
    "set-hook", "set-option", "set-window-option", "show-buffer",
    "show-environment", "show-hooks", "show-messages", "show-options",
    "show-prompt-history", "show-window-options", "source-file",
    "split-window", "start-server", "suspend-client", "swap-pane",
    "swap-window", "switch-client", "unbind-key", "unlink-window",
    "wait-for")
$requiredOptions = @(
    "default-command", "default-shell", "default-size", "display-time",
    "history-limit", "lock-command", "message-style", "mouse",
    "prefix", "repeat-time", "status", "status-left", "status-right",
    "update-environment")
$requiredServerOptions = @(
    "buffer-limit", "command-alias", "default-client-command",
    "default-terminal", "escape-time", "exit-empty", "focus-events",
    "history-file", "input-buffer-size", "message-limit", "set-clipboard",
    "terminal-features", "terminal-overrides", "user-keys")
$requiredWindowOptions = @(
    "aggressive-resize", "allow-rename", "automatic-rename",
    "copy-mode-match-style", "main-pane-width", "mode-keys",
    "monitor-activity", "pane-active-border-style", "pane-base-index",
    "pane-border-status", "remain-on-exit", "synchronize-panes",
    "window-size", "xterm-keys")
$requiredKeyTables = @("copy-mode", "copy-mode-vi", "prefix", "root")

try {
	$version = (Invoke-AuditTmux @("-V")).Out.Trim()
	Invoke-AuditTmux @("new-session", "-d", "-s", "audit",
	    "cmd.exe") | Out-Null
	$commandLines = Split-Lines (Invoke-AuditTmux @("list-commands")).Out
	$optionLines = Split-Lines (Invoke-AuditTmux @("show-options",
	    "-g")).Out
	$serverOptionLines = Split-Lines (Invoke-AuditTmux @("show-options",
	    "-s")).Out
	$windowOptionLines = Split-Lines (Invoke-AuditTmux @(
	    "show-window-options", "-g")).Out
	$keyLines = Split-Lines (Invoke-AuditTmux @("list-keys")).Out

	$commands = Get-CommandNames $commandLines
	$options = Get-OptionNames $optionLines
	$serverOptions = Get-OptionNames $serverOptionLines
	$windowOptions = Get-OptionNames $windowOptionLines
	$keyTables = @($keyLines | ForEach-Object {
	    if ($_ -match '^\s*bind-key\s+-T\s+(\S+)') {
		    $Matches[1]
	    }
	} | Sort-Object -Unique)

	Require-Minimum "command count" $commands.Count 90
	Require-Minimum "global option count" $options.Count 60
	Require-Minimum "server option count" $serverOptions.Count 25
	Require-Minimum "window option count" $windowOptions.Count 70
	Require-Minimum "key binding count" $keyLines.Count 250
	Require-Items "commands" $commands $requiredCommands
	Require-Items "global options" $options $requiredOptions
	Require-Items "server options" $serverOptions $requiredServerOptions
	Require-Items "window options" $windowOptions $requiredWindowOptions
	Require-Items "key tables" $keyTables $requiredKeyTables

	$defaultShell = Get-GlobalOptionValue "default-shell"
	$defaultTerminal = Get-GlobalOptionValue "default-terminal"
	$defaultCommand = Get-GlobalOptionValue "default-command"
	$lockCommand = Get-GlobalOptionValue "lock-command"
	$status = Get-GlobalOptionValue "status"
	$setClipboard = Get-ServerOptionValue "set-clipboard"
	$exitEmpty = Get-ServerOptionValue "exit-empty"
	$modeKeys = Get-WindowOptionValue "mode-keys"
	$windowSize = Get-WindowOptionValue "window-size"

	if ([string]::IsNullOrWhiteSpace($defaultShell)) {
		throw "default-shell is empty"
	}
	if (-not [System.IO.Path]::IsPathRooted($defaultShell)) {
		throw "default-shell is not an absolute Windows path: $defaultShell"
	}
	Require-EndsWith "default-shell" $defaultShell "\cmd.exe"
	Require-Equal "default-terminal" $defaultTerminal "tmux-win32"
	Require-Equal "default-command" $defaultCommand ""
	Require-Equal "lock-command" $lockCommand `
	    "rundll32.exe user32.dll,LockWorkStation"
	Require-Equal "status" $status "on"
	Require-Equal "set-clipboard" $setClipboard "external"
	Require-Equal "exit-empty" $exitEmpty "off"
	Require-Equal "mode-keys" $modeKeys "emacs"
	Require-Equal "window-size" $windowSize "latest"

	$optionDefaults = [pscustomobject]@{
		DefaultShell = $defaultShell
		DefaultTerminal = $defaultTerminal
		DefaultCommand = $defaultCommand
		LockCommand = $lockCommand
		Status = $status
		SetClipboard = $setClipboard
		ExitEmpty = $exitEmpty
		ModeKeys = $modeKeys
		WindowSize = $windowSize
	}

	if (-not [string]::IsNullOrWhiteSpace($SummaryPath)) {
		$summaryDirectory = Split-Path -Parent $SummaryPath
		if (-not [string]::IsNullOrWhiteSpace($summaryDirectory)) {
			New-Item -ItemType Directory -Force `
			    -Path $summaryDirectory | Out-Null
		}
		[pscustomobject]@{
			GeneratedUtc = [DateTime]::UtcNow.ToString("o")
			Tmux = $Tmux
			Version = $version
			CommandCount = $commands.Count
			GlobalOptionCount = $options.Count
			ServerOptionCount = $serverOptions.Count
			WindowOptionCount = $windowOptions.Count
			KeyBindingCount = $keyLines.Count
			KeyTables = $keyTables
			OptionDefaults = $optionDefaults
			RequiredCommands = $requiredCommands
			RequiredOptions = $requiredOptions
			RequiredServerOptions = $requiredServerOptions
			RequiredWindowOptions = $requiredWindowOptions
		} | ConvertTo-Json -Depth 4 |
		    Set-Content -LiteralPath $SummaryPath -Encoding ascii
	}

	Write-Host ("Windows command surface audit passed: {0} commands, {1} global options, {2} server options, {3} window options, {4} key bindings, Windows defaults verified" -f
	    $commands.Count, $options.Count, $serverOptions.Count,
	    $windowOptions.Count, $keyLines.Count)
} finally {
	Stop-AuditServer
}
