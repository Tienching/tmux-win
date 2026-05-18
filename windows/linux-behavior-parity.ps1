param(
	[string]$WindowsTmux = "",
	[string]$Wsl = "wsl.exe",
	[string]$Output = "",
	[int]$TimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($WindowsTmux)) {
	$WindowsTmux = Join-Path $Root "dist\tmux-win32-portable\tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($WindowsTmux)) {
	$WindowsTmux = Join-Path (Get-Location) $WindowsTmux
}
$WindowsTmux = (Resolve-Path -LiteralPath $WindowsTmux).Path

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "dist\linux-behavior-parity.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

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

function ConvertTo-ShSingleQuoted([string]$Value) {
	return "'" + $Value.Replace("'", "'\''") + "'"
}

function Invoke-CapturedProcess([string]$FileName, [string]$Arguments,
    [switch]$AllowFailure) {
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $FileName
	$psi.Arguments = $Arguments
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
		throw "process timed out: $FileName $Arguments"
	}
	$process.WaitForExit()
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Out = $stdoutTask.Result
		Err = $stderrTask.Result
	}
	if (-not $AllowFailure -and $result.ExitCode -ne 0) {
		throw @"
process failed: $FileName $Arguments
exit code: $($result.ExitCode)
stdout:
$($result.Out)
stderr:
$($result.Err)
"@
	}
	return $result
}

function Invoke-PlatformTmux([string]$Platform, [string]$ServerName,
    [string[]]$Arguments, [switch]$AllowFailure) {
	if ($Platform -eq "windows") {
		$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
		$argumentString = ($allArguments | ForEach-Object {
		    ConvertTo-WindowsArgument $_
		}) -join " "
		return Invoke-CapturedProcess $WindowsTmux $argumentString `
		    -AllowFailure:$AllowFailure
	}

	$allLinuxArguments = @("-L", $ServerName, "-f", "/dev/null") +
	    $Arguments
	$shellCommand = "tmux " + (($allLinuxArguments | ForEach-Object {
	    ConvertTo-ShSingleQuoted $_
	}) -join " ")
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	return Invoke-CapturedProcess $Wsl $wslArguments `
	    -AllowFailure:$AllowFailure
}

function Add-Result([System.Collections.Generic.List[object]]$Results,
    [string]$Platform, [string]$Name, [bool]$Passed, [string]$Detail) {
	$Results.Add([pscustomobject]@{
	    Platform = $Platform
	    Category = Get-BehaviorCategory $Name
	    Name = $Name
	    Passed = $Passed
	    Detail = $Detail
	})
}

function Get-BehaviorCategory([string]$Name) {
	switch ($Name) {
		"session lifecycle" { return "sessions" }
		"has-session exit codes" { return "sessions" }
		"session rename" { return "sessions" }
		"session group sharing" { return "sessions" }
		"kill-session" { return "sessions" }
		"wait-for lock/unlock" { return "sessions" }

		"window list shape" { return "windows" }
		"select-window active state" { return "windows" }
		"next/previous-window active state" { return "windows" }
		"last-window active state" { return "windows" }
		"select-layout" { return "windows" }
		"window rename" { return "windows" }
		"window link" { return "windows" }
		"window unlink" { return "windows" }
		"move-window" { return "windows" }
		"kill-window" { return "windows" }
		"swap-window" { return "windows" }
		"respawn-window" { return "windows" }

		"pane split count" { return "panes" }
		"rotate-window" { return "panes" }
		"swap-pane" { return "panes" }
		"kill-pane" { return "panes" }
		"break-pane" { return "panes" }
		"join-pane" { return "panes" }
		"respawn-pane" { return "panes" }
		"resize-pane" { return "panes" }
		"resize-pane zoom toggle" { return "panes" }
		"select-pane active state" { return "panes" }
		"last-pane active state" { return "panes" }
		"send-keys capture" { return "panes" }
		"paste-buffer input" { return "panes" }
		"capture-pane history range" { return "panes" }
		"pane output capture" { return "panes" }
		"pane format list" { return "panes" }

		"buffer round trip" { return "buffers" }
		"buffer append" { return "buffers" }
		"buffer save/load file" { return "buffers" }
		"buffer save append file" { return "buffers" }
		"buffer list/delete" { return "buffers" }
		"copy-mode copy-line" { return "copy-mode" }
		"copy-mode search navigation" { return "copy-mode" }
		"copy-mode history search" { return "copy-mode" }
		"copy-mode multi-line selection" { return "copy-mode" }
		"copy-mode rectangle selection" { return "copy-mode" }

		"global option set/show" { return "options" }
		"global option unset default" { return "options" }
		"server option set/show" { return "options" }
		"server option unset default" { return "options" }
		"window option set/show" { return "options" }
		"window option unset default" { return "options" }
		"user option set/show" { return "options" }
		"user option unset" { return "options" }
		"environment set/show" { return "environment" }
		"environment unset" { return "environment" }
		"environment pane inheritance" { return "environment" }
		"format expansion" { return "formats" }
		"pane current command/path formats" { return "formats" }
		"new-window cwd selection" { return "paths" }
		"run-shell cwd selection" { return "paths" }
		"dynamic pane cwd format" { return "paths" }
		"source-file config load" { return "configuration" }
		"key binding round trip" { return "key-bindings" }
		"key binding notes" { return "key-bindings" }

		"list-commands common entries" { return "commands" }
		"run-shell command mode" { return "commands" }
		"run-shell background job" { return "commands" }
		"if-shell format branch" { return "commands" }
		"show-messages command log" { return "commands" }
		"pipe-pane output capture" { return "commands" }
		"pipe-pane input injection" { return "commands" }
		"hook execution" { return "hooks" }
		"hook list" { return "hooks" }
		"control mode command client" { return "control-mode" }
		"version probe" { return "version" }
		default { return "uncategorized" }
	}
}

function Get-CategoryCoverage([object[]]$Results, [string[]]$Categories) {
	foreach ($category in $Categories) {
		$categoryResults = @($Results | Where-Object {
		    $_.Category -eq $category
		})
		$windows = @($categoryResults | Where-Object {
		    $_.Platform -eq "windows" -and $_.Passed
		})
		$linux = @($categoryResults | Where-Object {
		    $_.Platform -eq "linux" -and $_.Passed
		})
		[pscustomobject]@{
		    Category = $category
		    Covered = ($windows.Count -gt 0 -and $linux.Count -gt 0)
		    WindowsPassed = $windows.Count
		    LinuxPassed = $linux.Count
		}
	}
}

function Test-Contains([string]$Text, [string]$Expected) {
	return $Text -like "*$Expected*"
}

function Invoke-CheckedTmux([string]$Platform, [string]$ServerName,
    [string[]]$Arguments) {
	return Invoke-PlatformTmux $Platform $ServerName $Arguments
}

function Get-PlatformTempFile([string]$Platform, [string]$Name) {
	if ($Platform -eq "windows") {
		return (Join-Path ([System.IO.Path]::GetTempPath()) $Name)
	}
	return "/tmp/$Name"
}

function New-PlatformTempDirectory([string]$Platform, [string]$Name) {
	if ($Platform -eq "windows") {
		$path = Join-Path ([System.IO.Path]::GetTempPath()) $Name
		New-Item -ItemType Directory -Force -Path $path | Out-Null
		return $path
	}

	$path = "/tmp/$Name"
	$shellCommand = "mkdir -p " + (ConvertTo-ShSingleQuoted $path)
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	Invoke-CapturedProcess $Wsl $wslArguments | Out-Null
	return $path
}

function Get-PlatformFileContent([string]$Platform, [string]$Path) {
	if ($Platform -eq "windows") {
		if (-not (Test-Path -LiteralPath $Path)) {
			return ""
		}
		return Get-Content -LiteralPath $Path -Raw
	}

	$shellCommand = "cat " + (ConvertTo-ShSingleQuoted $Path)
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	$result = Invoke-CapturedProcess $Wsl $wslArguments -AllowFailure
	return $result.Out
}

function Wait-PlatformFileContains([string]$Platform, [string]$Path,
    [string]$Expected) {
	$content = ""
	foreach ($attempt in 1..20) {
		$content = Get-PlatformFileContent $Platform $Path
		if (Test-Contains $content $Expected) {
			return $content
		}
		Start-Sleep -Milliseconds 250
	}
	return $content
}

function Set-PlatformFileContent([string]$Platform, [string]$Path,
    [string[]]$Lines) {
	if ($Platform -eq "windows") {
		Set-Content -LiteralPath $Path -Encoding ascii -Value $Lines
		return
	}

	$quotedLines = ($Lines | ForEach-Object {
	    ConvertTo-ShSingleQuoted $_
	}) -join " "
	$shellCommand = "printf '%s\n' $quotedLines > " +
	    (ConvertTo-ShSingleQuoted $Path)
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	Invoke-CapturedProcess $Wsl $wslArguments | Out-Null
}

function Remove-PlatformFile([string]$Platform, [string]$Path) {
	if ($Platform -eq "windows") {
		Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
		return
	}

	$shellCommand = "rm -f " + (ConvertTo-ShSingleQuoted $Path)
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	Invoke-CapturedProcess $Wsl $wslArguments -AllowFailure | Out-Null
}

function Remove-PlatformDirectory([string]$Platform, [string]$Path) {
	if ($Platform -eq "windows") {
		if (Test-Path -LiteralPath $Path -PathType Container) {
			Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
		}
		return
	}

	$shellCommand = "rmdir " + (ConvertTo-ShSingleQuoted $Path)
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	Invoke-CapturedProcess $Wsl $wslArguments -AllowFailure | Out-Null
}

function Run-PlatformCases([string]$Platform,
    [System.Collections.Generic.List[object]]$Results) {
	$serverName = "behavior-$Platform-" + [Guid]::NewGuid().ToString("N")
	$shell = if ($Platform -eq "windows") { "cmd.exe" } else { "sh" }
	$session = "parity"

	try {
		$version = (Invoke-CheckedTmux $Platform $serverName @("-V")).Out.
		    Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-session", "-d", "-s", $session, $shell) | Out-Null
		$sessions = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-sessions", "-F", "#{session_name}:#{session_windows}")).Out
		Add-Result $Results $Platform "session lifecycle" `
		    (Test-Contains $sessions "$session`:1") `
		    $sessions.Trim()

		$hasExistingSession = Invoke-PlatformTmux $Platform $serverName @(
		    "has-session", "-t", $session) -AllowFailure
		$hasMissingSession = Invoke-PlatformTmux $Platform $serverName @(
		    "has-session", "-t", "missing-session") -AllowFailure
		Add-Result $Results $Platform "has-session exit codes" `
		    ($hasExistingSession.ExitCode -eq 0 -and
		    $hasMissingSession.ExitCode -eq 1) `
		    ("existing={0};missing={1}" -f
		    $hasExistingSession.ExitCode, $hasMissingSession.ExitCode)

		Invoke-CheckedTmux $Platform $serverName @(
		    "rename-session", "-t", $session, "renamed-session") |
		    Out-Null
		$renamedSession = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-sessions", "-F", "#{session_name}")).Out.Trim()
		Add-Result $Results $Platform "session rename" `
		    (Test-Contains $renamedSession "renamed-session") `
		    $renamedSession
		Invoke-CheckedTmux $Platform $serverName @(
		    "rename-session", "-t", "renamed-session", $session) |
		    Out-Null

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-session", "-d", "-t", $session, "-s",
		    "groupmate") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n",
		    "grouped", $shell) | Out-Null
		$groupSessions = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-sessions", "-F",
		    "#{session_name}:#{session_group}:#{session_windows}")).Out
		$groupMateWindows = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", "groupmate", "-F",
		    "#{window_name}")).Out
		Add-Result $Results $Platform "session group sharing" `
		    ((Test-Contains $groupSessions "$session`:$session`:2") -and
		    (Test-Contains $groupSessions "groupmate:$session`:2") -and
		    (Test-Contains $groupMateWindows "grouped")) `
		    "sessions=$($groupSessions.Trim());windows=$($groupMateWindows.Trim())"
		Invoke-CheckedTmux $Platform $serverName @(
		    "kill-session", "-t", "groupmate") | Out-Null
		$sessionsAfterKill = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-sessions", "-F", "#{session_name}")).Out
		Add-Result $Results $Platform "kill-session" `
		    ((Test-Contains $sessionsAfterKill $session) -and
		    -not (Test-Contains $sessionsAfterKill "groupmate")) `
		    $sessionsAfterKill.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "kill-window", "-t", "$session`:grouped") | Out-Null

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "extra", $shell) |
		    Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-t", "$session`:0", $shell) |
		    Out-Null
		$panes = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:0", "-F", "#{pane_index}")).
		    Out -split "`r?`n" | Where-Object {
			-not [string]::IsNullOrWhiteSpace($_)
		    })
		Add-Result $Results $Platform "pane split count" `
		    ($panes.Count -eq 2) ("panes=$($panes -join ',')")

		Invoke-CheckedTmux $Platform $serverName @(
		    "select-pane", "-t", "$session`:0.1") | Out-Null
		$activePanes = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:0", "-F",
		    "#{pane_index}:#{pane_active}")).Out
		Add-Result $Results $Platform "select-pane active state" `
		    ((Test-Contains $activePanes "0:0") -and
		    (Test-Contains $activePanes "1:1")) `
		    $activePanes.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "last-pane", "-t", "$session`:0") | Out-Null
		$lastPaneActive = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:0", "-F",
		    "#{pane_index}:#{pane_active}")).Out
		Add-Result $Results $Platform "last-pane active state" `
		    ((Test-Contains $lastPaneActive "0:1") -and
		    (Test-Contains $lastPaneActive "1:0")) `
		    $lastPaneActive.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "select-pane", "-t", "$session`:0.0") | Out-Null

		Invoke-CheckedTmux $Platform $serverName @(
		    "resize-pane", "-Z", "-t", "$session`:0.1") | Out-Null
		$zoomedFlag = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0",
		    "#{window_zoomed_flag}")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "resize-pane", "-Z", "-t", "$session`:0.1") | Out-Null
		$unzoomedFlag = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0",
		    "#{window_zoomed_flag}")).Out.Trim()
		Add-Result $Results $Platform "resize-pane zoom toggle" `
		    ($zoomedFlag -eq "1" -and $unzoomedFlag -eq "0") `
		    ("zoomed={0};unzoomed={1}" -f $zoomedFlag, $unzoomedFlag)

		$windows = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_panes}")).Out
		$windowShapePassed = (Test-Contains $windows "extra:1") -and
		    ((Test-Contains $windows "cmd.exe:2") -or
		    (Test-Contains $windows "cmd:2") -or
		    (Test-Contains $windows "sh:2") -or
		    (Test-Contains $windows "0:2"))
		Add-Result $Results $Platform "window list shape" `
		    $windowShapePassed $windows.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "select-window", "-t", "$session`:extra") | Out-Null
		$activeWindows = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_active}")).Out
		Add-Result $Results $Platform "select-window active state" `
		    (Test-Contains $activeWindows "extra:1") `
		    $activeWindows.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "select-window", "-t", "$session`:0") | Out-Null

		Invoke-CheckedTmux $Platform $serverName @(
		    "next-window", "-t", $session) | Out-Null
		$nextWindowActive = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_active}")).Out
		Invoke-CheckedTmux $Platform $serverName @(
		    "previous-window", "-t", $session) | Out-Null
		$previousWindowActive = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_active}")).Out
		Add-Result $Results $Platform "next/previous-window active state" `
		    ((Test-Contains $nextWindowActive "extra:1") -and
		    (Test-Contains $previousWindowActive "extra:0")) `
		    ("next={0};previous={1}" -f $nextWindowActive.Trim(),
		    $previousWindowActive.Trim())

		Invoke-CheckedTmux $Platform $serverName @(
		    "select-window", "-t", "$session`:0") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "select-window", "-t", "$session`:extra") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "last-window", "-t", $session) | Out-Null
		$lastWindowActive = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_active}")).Out
		Add-Result $Results $Platform "last-window active state" `
		    ((Test-Contains $lastWindowActive "extra:0") -and
		    -not (Test-Contains $lastWindowActive "extra:1")) `
		    $lastWindowActive.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-buffer",
		    "PARITY_BUFFER_OK") | Out-Null
		$buffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer", "-b", "parity-buffer")).Out.Trim()
		Add-Result $Results $Platform "buffer round trip" `
		    ($buffer -eq "PARITY_BUFFER_OK") $buffer

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-append-buffer",
		    "PARITY_APPEND_A") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-ab", "parity-append-buffer",
		    "_B") | Out-Null
		$appendBuffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer", "-b", "parity-append-buffer")).Out.Trim()
		Add-Result $Results $Platform "buffer append" `
		    ($appendBuffer -eq "PARITY_APPEND_A_B") $appendBuffer

		$bufferFile = Get-PlatformTempFile $Platform `
		    ("tmux-parity-buffer-" + [Guid]::NewGuid().ToString("N") +
		    ".txt")
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-file-buffer",
		    "PARITY_FILE_BUFFER_OK") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "save-buffer", "-b", "parity-file-buffer", $bufferFile) |
		    Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "delete-buffer", "-b", "parity-file-buffer") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "load-buffer", "-b", "parity-file-buffer", $bufferFile) |
		    Out-Null
		$fileBuffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer", "-b", "parity-file-buffer")).Out.Trim()
		Add-Result $Results $Platform "buffer save/load file" `
		    ($fileBuffer -eq "PARITY_FILE_BUFFER_OK") $fileBuffer

		Set-PlatformFileContent $Platform $bufferFile @("PARITY_FILE_HEAD")
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-file-append-buffer",
		    "PARITY_FILE_TAIL") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "save-buffer", "-a", "-b", "parity-file-append-buffer",
		    $bufferFile) | Out-Null
		$appendedBufferFile = (Get-PlatformFileContent $Platform `
		    $bufferFile).Trim()
		$appendedBufferFileNormalized = $appendedBufferFile `
		    -replace "`r`n", "`n"
		Remove-PlatformFile $Platform $bufferFile
		Add-Result $Results $Platform "buffer save append file" `
		    ($appendedBufferFileNormalized -eq `
		    "PARITY_FILE_HEAD`nPARITY_FILE_TAIL") `
		    $appendedBufferFileNormalized

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-list-a",
		    "PARITY_LIST_A") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-list-b",
		    "PARITY_LIST_B") | Out-Null
		$buffersBeforeDelete = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-buffers", "-F", "#{buffer_name}")).Out
		Invoke-CheckedTmux $Platform $serverName @(
		    "delete-buffer", "-b", "parity-list-a") | Out-Null
		$buffersAfterDelete = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-buffers", "-F", "#{buffer_name}")).Out
		Add-Result $Results $Platform "buffer list/delete" `
		    ((Test-Contains $buffersBeforeDelete "parity-list-a") -and
		    (Test-Contains $buffersBeforeDelete "parity-list-b") -and
		    -not (Test-Contains $buffersAfterDelete "parity-list-a") -and
		    (Test-Contains $buffersAfterDelete "parity-list-b")) `
		    "before=$($buffersBeforeDelete.Trim());after=$($buffersAfterDelete.Trim())"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "status", "off") | Out-Null
		$status = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-gqv", "status")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "status", "on") | Out-Null
		Add-Result $Results $Platform "global option set/show" `
		    ($status -eq "off") "status=$status"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "status", "off") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gu", "status") | Out-Null
		$statusAfterUnset = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-gqv", "status")).Out.Trim()
		Add-Result $Results $Platform "global option unset default" `
		    ($statusAfterUnset -eq "on") "status=$statusAfterUnset"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-sq", "escape-time", "321") | Out-Null
		$escapeTime = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-sqv", "escape-time")).Out.Trim()
		Add-Result $Results $Platform "server option set/show" `
		    ($escapeTime -eq "321") "escape-time=$escapeTime"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-su", "escape-time") | Out-Null
		$escapeTimeAfterUnset = (Invoke-CheckedTmux $Platform `
		    $serverName @("show-options", "-sqv", "escape-time")).Out.
		    Trim()
		Add-Result $Results $Platform "server option unset default" `
		    ($escapeTimeAfterUnset -eq "10") `
		    "escape-time=$escapeTimeAfterUnset"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "@parity-user-option", "OK value") |
		    Out-Null
		$userOption = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-gqv", "@parity-user-option")).Out.Trim()
		Add-Result $Results $Platform "user option set/show" `
		    ($userOption -eq "OK value") "@parity-user-option=$userOption"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gu", "@parity-user-option") | Out-Null
		$userOptionAfterUnset = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-gqv", "@parity-user-option")).Out.Trim()
		Add-Result $Results $Platform "user option unset" `
		    ($userOptionAfterUnset -eq "") `
		    "@parity-user-option=$userOptionAfterUnset"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-environment", "-g", "PARITY_ENV", "OK") | Out-Null
		$environment = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-environment", "-g", "PARITY_ENV")).Out.Trim()
		Add-Result $Results $Platform "environment set/show" `
		    ($environment -eq "PARITY_ENV=OK") $environment

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-environment", "-g", "PARITY_REMOVE_ME", "OK") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-environment", "-gu", "PARITY_REMOVE_ME") | Out-Null
		$environmentUnset = Invoke-PlatformTmux $Platform $serverName @(
		    "show-environment", "-g", "PARITY_REMOVE_ME") -AllowFailure
		Add-Result $Results $Platform "environment unset" `
		    ($environmentUnset.ExitCode -eq 1 -and
		    (Test-Contains $environmentUnset.Err "unknown variable")) `
		    ("exit={0};err={1}" -f $environmentUnset.ExitCode,
		    $environmentUnset.Err.Trim())

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-environment", "-g", "PARITY_CHILD_ENV", "OK") |
		    Out-Null
		$environmentCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c set PARITY_CHILD_ENV & timeout /t 30 /nobreak >NUL"
		} else {
			"sh -lc 'env | grep ^PARITY_CHILD_ENV=; sleep 30'"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "envcase",
		    $environmentCommand) | Out-Null
		Start-Sleep -Milliseconds 1000
		$environmentCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:envcase")).Out
		Add-Result $Results $Platform "environment pane inheritance" `
		    (Test-Contains $environmentCapture "PARITY_CHILD_ENV=OK") `
		    $environmentCapture.Trim()

		$cwdDirectory = New-PlatformTempDirectory $Platform `
		    ("tmux cwd " + [Guid]::NewGuid().ToString("N").Substring(0, 8))
		$cwdCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c cd & timeout /t 30 /nobreak >NUL"
		} else {
			"sh -lc 'pwd; sleep 30'"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "cwdcase",
		    "-c", $cwdDirectory, $cwdCommand) | Out-Null
		Start-Sleep -Milliseconds 1000
		$cwdCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:cwdcase")).Out
		Remove-PlatformDirectory $Platform $cwdDirectory
		Add-Result $Results $Platform "new-window cwd selection" `
		    (Test-Contains $cwdCapture $cwdDirectory) `
		    $cwdCapture.Trim()

		$dynamicCwdDirectory = New-PlatformTempDirectory $Platform `
		    ("tmuxdyn" + [Guid]::NewGuid().ToString("N").Substring(0, 8))
		$dynamicCwdCommand = if ($Platform -eq "windows") {
			"cd /d $dynamicCwdDirectory"
		} else {
			"cd $dynamicCwdDirectory"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:0.0",
		    $dynamicCwdCommand, "Enter") | Out-Null
		Start-Sleep -Milliseconds 1000
		$dynamicCwd = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0.0",
		    "#{pane_current_path}")).Out.Trim()
		Remove-PlatformDirectory $Platform $dynamicCwdDirectory
		Add-Result $Results $Platform "dynamic pane cwd format" `
		    ($dynamicCwd -eq $dynamicCwdDirectory) `
		    $dynamicCwd

		$format = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0.0",
		    "#{session_name}:#{window_panes}:#{pane_index}")).Out.Trim()
		Add-Result $Results $Platform "format expansion" `
		    ($format -eq "$session`:2:0") $format

		$paneFormat = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0.0",
		    "#{pane_current_command}|#{pane_current_path}|#{pane_pid}")).
		    Out.Trim()
		$paneFormatParts = @($paneFormat -split "\|", 3)
		$paneCommand = if ($paneFormatParts.Count -ge 1) {
			$paneFormatParts[0]
		} else {
			""
		}
		$panePath = if ($paneFormatParts.Count -ge 2) {
			$paneFormatParts[1]
		} else {
			""
		}
		$panePid = if ($paneFormatParts.Count -ge 3) {
			$paneFormatParts[2]
		} else {
			""
		}
		$paneFormatPassed = if ($Platform -eq "windows") {
			(($paneCommand -eq "cmd.exe") -or ($paneCommand -eq "cmd")) -and
			    -not (Test-Contains $paneCommand "conhost") -and
			    -not (Test-Contains $paneCommand "OpenConsole") -and
			    (Test-Contains $panePath "tmux") -and
			    -not [string]::IsNullOrWhiteSpace($panePid)
		} else {
			($paneCommand -eq "sh") -and
			    (Test-Contains $panePath "tmux") -and
			    -not [string]::IsNullOrWhiteSpace($panePid)
		}
		Add-Result $Results $Platform "pane current command/path formats" `
		    $paneFormatPassed $paneFormat

		$configFile = Get-PlatformTempFile $Platform `
		    ("tmux-parity-source-" + [Guid]::NewGuid().ToString("N") +
		    ".conf")
		Set-PlatformFileContent $Platform $configFile @(
		    "set-environment -g PARITY_SOURCE_FILE OK",
		    "set-option -gq status off"
		)
		Invoke-CheckedTmux $Platform $serverName @(
		    "source-file", $configFile) | Out-Null
		$sourceEnvironment = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-environment", "-g", "PARITY_SOURCE_FILE")).Out.Trim()
		$sourceStatus = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-gqv", "status")).Out.Trim()
		Remove-PlatformFile $Platform $configFile
		Add-Result $Results $Platform "source-file config load" `
		    ($sourceEnvironment -eq "PARITY_SOURCE_FILE=OK" -and
		    $sourceStatus -eq "off") `
		    "env=$sourceEnvironment;status=$sourceStatus"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-wq", "-t", "$session`:0",
		    "pane-border-status", "top") | Out-Null
		$windowOption = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-options", "-wqv", "-t", "$session`:0",
		    "pane-border-status")).Out.Trim()
		Add-Result $Results $Platform "window option set/show" `
		    ($windowOption -eq "top") "pane-border-status=$windowOption"

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-wu", "-t", "$session`:0",
		    "pane-border-status") | Out-Null
		$windowOptionAfterUnset = (Invoke-CheckedTmux $Platform `
		    $serverName @("show-options", "-wqv", "-t", "$session`:0",
		    "pane-border-status")).Out.Trim()
		Add-Result $Results $Platform "window option unset default" `
		    ($windowOptionAfterUnset -eq "") `
		    "pane-border-status=$windowOptionAfterUnset"

		$layoutResult = Invoke-PlatformTmux $Platform $serverName @(
		    "select-layout", "-t", "$session`:0", "even-horizontal") `
		    -AllowFailure
		$layout = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:0",
		    "#{window_layout}")).Out.Trim()
		Add-Result $Results $Platform "select-layout" `
		    ($layoutResult.ExitCode -eq 0 -and
		    -not [string]::IsNullOrWhiteSpace($layout)) `
		    "exit=$($layoutResult.ExitCode);layout=$layout"

		$runShell = (Invoke-CheckedTmux $Platform $serverName @(
		    "run-shell", "-C",
		    "display-message -p PARITY_RUNSHELL_C_OK")).Out.Trim()
		Add-Result $Results $Platform "run-shell command mode" `
		    ($runShell -eq "PARITY_RUNSHELL_C_OK") $runShell

		$runShellFile = Get-PlatformTempFile $Platform `
		    ("tmux-parity-runshell-b-" + [Guid]::NewGuid().ToString("N") +
		    ".txt")
		Remove-PlatformFile $Platform $runShellFile
		$runShellBackgroundCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c echo PARITY_RUNSHELL_B_OK>`"$runShellFile`""
		} else {
			"sh -lc 'echo PARITY_RUNSHELL_B_OK > $runShellFile'"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "run-shell", "-b", $runShellBackgroundCommand) | Out-Null
		$runShellBackground = Wait-PlatformFileContains $Platform `
		    $runShellFile "PARITY_RUNSHELL_B_OK"
		Remove-PlatformFile $Platform $runShellFile
		Add-Result $Results $Platform "run-shell background job" `
		    (Test-Contains $runShellBackground "PARITY_RUNSHELL_B_OK") `
		    $runShellBackground.Trim()

		$runShellCwdDirectory = New-PlatformTempDirectory $Platform `
		    ("tmux runcwd " + [Guid]::NewGuid().ToString("N").Substring(0, 8))
		$runShellCwdFile = Get-PlatformTempFile $Platform `
		    ("tmux-parity-runshell-c-" + [Guid]::NewGuid().ToString("N") +
		    ".txt")
		Remove-PlatformFile $Platform $runShellCwdFile
		$runShellCwdCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c cd > `"$runShellCwdFile`""
		} else {
			"sh -lc 'pwd > $runShellCwdFile'"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "run-shell", "-c", $runShellCwdDirectory,
		    $runShellCwdCommand) | Out-Null
		$runShellCwd = Wait-PlatformFileContains $Platform `
		    $runShellCwdFile $runShellCwdDirectory
		Remove-PlatformFile $Platform $runShellCwdFile
		Remove-PlatformDirectory $Platform $runShellCwdDirectory
		Add-Result $Results $Platform "run-shell cwd selection" `
		    (Test-Contains $runShellCwd $runShellCwdDirectory) `
		    $runShellCwd.Trim()

		$ifShell = (Invoke-CheckedTmux $Platform $serverName @(
		    "if-shell", "-F", "-t", "$session`:0",
		    "#{==:#{session_name},$session}",
		    "display-message -p PARITY_IF_TRUE",
		    "display-message -p PARITY_IF_FALSE")).Out.Trim()
		Add-Result $Results $Platform "if-shell format branch" `
		    ($ifShell -eq "PARITY_IF_TRUE") $ifShell

		$commandList = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-commands")).Out
		Add-Result $Results $Platform "list-commands common entries" `
		    ((Test-Contains $commandList "new-session") -and
		    (Test-Contains $commandList "split-window") -and
		    (Test-Contains $commandList "display-message") -and
		    (Test-Contains $commandList "source-file")) `
		    $commandList.Trim()

		$messageLog = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-messages")).Out
		Add-Result $Results $Platform "show-messages command log" `
		    ((Test-Contains $messageLog "show-messages") -and
		    (Test-Contains $messageLog "list-commands") -and
		    (Test-Contains $messageLog "if-shell")) `
		    $messageLog.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "bind-key", "-T", "prefix", "F12",
		    "display-message", "-p", "PARITY_BIND_OK") | Out-Null
		$keysBeforeUnbind = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-keys", "-T", "prefix")).Out
		Invoke-CheckedTmux $Platform $serverName @(
		    "unbind-key", "-T", "prefix", "F12") | Out-Null
		$keysAfterUnbind = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-keys", "-T", "prefix")).Out
		Add-Result $Results $Platform "key binding round trip" `
		    ((Test-Contains $keysBeforeUnbind "PARITY_BIND_OK") -and
		    -not (Test-Contains $keysAfterUnbind "PARITY_BIND_OK")) `
		    "before=$($keysBeforeUnbind -like '*PARITY_BIND_OK*');after=$($keysAfterUnbind -like '*PARITY_BIND_OK*')"

		Invoke-CheckedTmux $Platform $serverName @(
		    "bind-key", "-T", "prefix", "-N", "parity note",
		    "F12", "display-message", "-p", "PARITY_BIND_NOTE") |
		    Out-Null
		$keyNotes = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-keys", "-N")).Out
		Invoke-CheckedTmux $Platform $serverName @(
		    "unbind-key", "-T", "prefix", "F12") | Out-Null
		Add-Result $Results $Platform "key binding notes" `
		    ((Test-Contains $keyNotes "F12") -and
		    (Test-Contains $keyNotes "parity note")) `
		    $keyNotes.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "rename-window", "-t", "$session`:1", "renamed") |
		    Out-Null
		$renamed = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_panes}")).Out
		Add-Result $Results $Platform "window rename" `
		    (Test-Contains $renamed "renamed:1") $renamed.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "link-window", "-s", "$session`:renamed", "-t",
		    "$session`:9") | Out-Null
		$linked = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_index}:#{window_name}:#{window_panes}")).Out
		$linkPassed = (Test-Contains $linked "1:renamed:1") -and
		    (Test-Contains $linked "9:renamed:1")
		Add-Result $Results $Platform "window link" `
		    $linkPassed $linked.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "unlink-window", "-t", "$session`:9") | Out-Null
		$unlinked = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_index}:#{window_name}")).Out
		Add-Result $Results $Platform "window unlink" `
		    (-not (Test-Contains $unlinked "9:renamed")) `
		    $unlinked.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "move-src",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "move-window", "-s", "$session`:move-src", "-t",
		    "$session`:7") | Out-Null
		$moved = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_index}:#{window_name}")).Out
		Add-Result $Results $Platform "move-window" `
		    (Test-Contains $moved "7:move-src") $moved.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "kill-window", "-t", "$session`:7") | Out-Null
		$killedWindow = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_index}:#{window_name}")).Out
		Add-Result $Results $Platform "kill-window" `
		    (-not (Test-Contains $killedWindow "7:move-src")) `
		    $killedWindow.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", "$session`:20", "-n", "swapa",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", "$session`:21", "-n", "swapb",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "swap-window", "-s", "$session`:20", "-t",
		    "$session`:21") | Out-Null
		$swappedWindows = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_index}:#{window_name}")).Out
		Add-Result $Results $Platform "swap-window" `
		    ((Test-Contains $swappedWindows "20:swapb") -and
		    (Test-Contains $swappedWindows "21:swapa")) `
		    $swappedWindows.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "swapcase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-t", "$session`:swapcase",
		    $shell) | Out-Null
		$swapPanesBefore = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:swapcase", "-F",
		    "#{pane_index}:#{pane_id}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		$swapPaneZeroId = (($swapPanesBefore | Where-Object {
		    $_ -like "0:*"
		}) -split ":", 2)[1]
		$swapPaneOneId = (($swapPanesBefore | Where-Object {
		    $_ -like "1:*"
		}) -split ":", 2)[1]
		Invoke-CheckedTmux $Platform $serverName @(
		    "swap-pane", "-s", "$session`:swapcase.0", "-t",
		    "$session`:swapcase.1") | Out-Null
		$swappedPanes = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:swapcase", "-F",
		    "#{pane_index}:#{pane_id}")).Out
		Add-Result $Results $Platform "swap-pane" `
		    ((Test-Contains $swappedPanes "0:$swapPaneOneId") -and
		    (Test-Contains $swappedPanes "1:$swapPaneZeroId")) `
		    "before=$($swapPanesBefore -join ',');after=$($swappedPanes.Trim())"

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "rotatecase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-t", "$session`:rotatecase",
		    $shell) | Out-Null
		$rotatePanesBefore = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:rotatecase", "-F",
		    "#{pane_index}:#{pane_id}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		$rotatePaneZeroId = (($rotatePanesBefore | Where-Object {
		    $_ -like "0:*"
		}) -split ":", 2)[1]
		$rotatePaneOneId = (($rotatePanesBefore | Where-Object {
		    $_ -like "1:*"
		}) -split ":", 2)[1]
		Invoke-CheckedTmux $Platform $serverName @(
		    "rotate-window", "-t", "$session`:rotatecase") | Out-Null
		$rotatedPanes = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:rotatecase", "-F",
		    "#{pane_index}:#{pane_id}")).Out
		Add-Result $Results $Platform "rotate-window" `
		    ((Test-Contains $rotatedPanes "0:$rotatePaneOneId") -and
		    (Test-Contains $rotatedPanes "1:$rotatePaneZeroId")) `
		    "before=$($rotatePanesBefore -join ',');after=$($rotatedPanes.Trim())"

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "killcase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-t", "$session`:killcase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "kill-pane", "-t", "$session`:killcase.1") | Out-Null
		$killPanePanes = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:killcase",
		    "-F", "#{pane_index}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		Add-Result $Results $Platform "kill-pane" `
		    ($killPanePanes.Count -eq 1) `
		    "panes=$($killPanePanes -join ',')"

		Invoke-CheckedTmux $Platform $serverName @(
		    "break-pane", "-d", "-s", "$session`:0.1", "-n",
		    "broken") | Out-Null
		$broken = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-windows", "-t", $session, "-F",
		    "#{window_name}:#{window_panes}")).Out
		Add-Result $Results $Platform "break-pane" `
		    (Test-Contains $broken "broken:1") $broken.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "join-pane", "-d", "-s", "$session`:broken.0", "-t",
		    "$session`:0") | Out-Null
		$joinedPanes = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:0", "-F", "#{pane_index}")).
		    Out -split "`r?`n" | Where-Object {
			-not [string]::IsNullOrWhiteSpace($_)
		    })
		Add-Result $Results $Platform "join-pane" `
		    ($joinedPanes.Count -eq 2) `
		    ("panes=$($joinedPanes -join ',')")

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-hook", "-g", "after-new-window",
		    "set-environment -g PARITY_HOOK OK") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "hooked",
		    $shell) | Out-Null
		Start-Sleep -Milliseconds 500
		$hook = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-environment", "-g", "PARITY_HOOK")).Out.Trim()
		Add-Result $Results $Platform "hook execution" `
		    ($hook -eq "PARITY_HOOK=OK") $hook

		$hookList = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-hooks", "-g", "after-new-window")).Out
		Add-Result $Results $Platform "hook list" `
		    ((Test-Contains $hookList "after-new-window") -and
		    (Test-Contains $hookList "PARITY_HOOK")) `
		    $hookList.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "respawn",
		    $shell) | Out-Null
		$respawn = Invoke-PlatformTmux $Platform $serverName @(
		    "respawn-pane", "-k", "-t", "$session`:respawn.0",
		    $shell) -AllowFailure
		$respawnPanes = @((Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:respawn",
		    "-F", "#{pane_index}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		Add-Result $Results $Platform "respawn-pane" `
		    ($respawn.ExitCode -eq 0 -and $respawnPanes.Count -eq 1) `
		    "exit=$($respawn.ExitCode);panes=$($respawnPanes -join ',')"

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "respawnwin",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-t", "$session`:respawnwin",
		    $shell) | Out-Null
		$respawnWindowBefore = @((Invoke-CheckedTmux $Platform `
		    $serverName @("list-panes", "-t", "$session`:respawnwin",
		    "-F", "#{pane_index}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		Invoke-CheckedTmux $Platform $serverName @(
		    "respawn-window", "-k", "-t", "$session`:respawnwin",
		    $shell) | Out-Null
		$respawnWindowAfter = @((Invoke-CheckedTmux $Platform `
		    $serverName @("list-panes", "-t", "$session`:respawnwin",
		    "-F", "#{pane_index}")).Out -split "`r?`n" |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		Add-Result $Results $Platform "respawn-window" `
		    ($respawnWindowBefore.Count -eq 2 -and
		    $respawnWindowAfter.Count -eq 1) `
		    "before=$($respawnWindowBefore.Count);after=$($respawnWindowAfter.Count)"

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "resizecase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "window-size", "manual") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "resize-window", "-t", "$session`:resizecase",
		    "-x", "80", "-y", "24") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "split-window", "-d", "-h", "-t", "$session`:resizecase",
		    $shell) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "resize-pane", "-t", "$session`:resizecase.0",
		    "-x", "30") | Out-Null
		$resizePanes = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-t", "$session`:resizecase",
		    "-F", "#{pane_index}:#{pane_width}x#{pane_height}")).Out
		Add-Result $Results $Platform "resize-pane" `
		    ((Test-Contains $resizePanes "0:30x24") -and
		    (Test-Contains $resizePanes "1:49x24")) `
		    $resizePanes.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "set-option", "-gq", "window-size", "latest") | Out-Null

		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "keyio",
		    $shell) | Out-Null
		Start-Sleep -Milliseconds 500
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_KEYS_OK", "Enter") | Out-Null
		Start-Sleep -Milliseconds 500
		$keyCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:keyio")).Out
		Add-Result $Results $Platform "send-keys capture" `
		    (Test-Contains $keyCapture "PARITY_KEYS_OK") `
		    $keyCapture.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "set-buffer", "-b", "parity-paste",
		    "echo PARITY_PASTE_OK") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "paste-buffer", "-b", "parity-paste", "-t",
		    "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio", "Enter") |
		    Out-Null
		Start-Sleep -Milliseconds 500
		$pasteCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:keyio")).Out
		Add-Result $Results $Platform "paste-buffer input" `
		    (Test-Contains $pasteCapture "PARITY_PASTE_OK") `
		    $pasteCapture.Trim()

		$pipeOutputFile = Get-PlatformTempFile $Platform `
		    ("tmux-parity-pipe-out-" + [Guid]::NewGuid().ToString("N") +
		    ".txt")
		Remove-PlatformFile $Platform $pipeOutputFile
		$pipeOutputCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c more > `"$pipeOutputFile`""
		} else {
			"cat > " + (ConvertTo-ShSingleQuoted $pipeOutputFile)
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "pipe-pane", "-O", "-t", "$session`:keyio",
		    $pipeOutputCommand) | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_PIPE_OUT_OK", "Enter") | Out-Null
		Start-Sleep -Milliseconds 1000
		Invoke-CheckedTmux $Platform $serverName @(
		    "pipe-pane", "-t", "$session`:keyio") | Out-Null
		Start-Sleep -Milliseconds 500
		$pipeOutput = Get-PlatformFileContent $Platform $pipeOutputFile
		Remove-PlatformFile $Platform $pipeOutputFile
		Add-Result $Results $Platform "pipe-pane output capture" `
		    (Test-Contains $pipeOutput "PARITY_PIPE_OUT_OK") `
		    $pipeOutput.Trim()

		$pipeInputCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c echo echo PARITY_PIPE_IN_OK"
		} else {
			"echo echo PARITY_PIPE_IN_OK"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "pipe-pane", "-I", "-t", "$session`:keyio",
		    $pipeInputCommand) | Out-Null
		Start-Sleep -Milliseconds 1000
		$pipeInputCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:keyio")).Out
		Add-Result $Results $Platform "pipe-pane input injection" `
		    (Test-Contains $pipeInputCapture "PARITY_PIPE_IN_OK") `
		    $pipeInputCapture.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_COPY_LINE_OK", "Enter") | Out-Null
		Start-Sleep -Milliseconds 500
		Invoke-CheckedTmux $Platform $serverName @(
		    "copy-mode", "-t", "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-backward", "PARITY_COPY_LINE_OK") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "copy-line-and-cancel") | Out-Null
		$copyLineBuffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer")).Out
		Add-Result $Results $Platform "copy-mode copy-line" `
		    (Test-Contains $copyLineBuffer "PARITY_COPY_LINE_OK") `
		    $copyLineBuffer.Trim()

		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_SEARCH_ALPHA", "Enter") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_SEARCH_BETA", "Enter") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    "echo PARITY_SEARCH_GAMMA", "Enter") | Out-Null
		Start-Sleep -Milliseconds 700
		Invoke-CheckedTmux $Platform $serverName @(
		    "copy-mode", "-t", "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-backward", "PARITY_SEARCH_ALPHA") | Out-Null
		$searchBackward = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:keyio",
		    "#{copy_cursor_line}")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-forward", "PARITY_SEARCH_GAMMA") | Out-Null
		$searchForward = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:keyio",
		    "#{copy_cursor_line}")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio", "cancel") |
		    Out-Null
		Add-Result $Results $Platform "copy-mode search navigation" `
		    ((Test-Contains $searchBackward "PARITY_SEARCH_ALPHA") -and
		    (Test-Contains $searchForward "PARITY_SEARCH_GAMMA")) `
		    "backward=$searchBackward;forward=$searchForward"

		$historyCommand = if ($Platform -eq "windows") {
			'for /l %i in (1,1,30) do @echo PARITY_HIST_%i'
		} else {
			"printf '%s\n' " + ((1..30 | ForEach-Object {
			    "PARITY_HIST_$_"
			}) -join " ")
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    $historyCommand, "Enter") | Out-Null
		Start-Sleep -Milliseconds 1000
		$historyCapture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-S", "-100", "-t",
		    "$session`:keyio")).Out
		$historyHas7 = Test-Contains $historyCapture "PARITY_HIST_7"
		$historyHas30 = Test-Contains $historyCapture "PARITY_HIST_30"
		$historyLineCount = @($historyCapture -split "`r?`n").Count
		Add-Result $Results $Platform "capture-pane history range" `
		    ($historyHas7 -and $historyHas30) `
		    ("has7={0};has30={1};lines={2}" -f $historyHas7,
		    $historyHas30, $historyLineCount)

		Invoke-CheckedTmux $Platform $serverName @(
		    "copy-mode", "-t", "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-backward", "PARITY_HIST_7") | Out-Null
		$historyBackward = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:keyio",
		    "#{copy_cursor_line}")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-forward", "PARITY_HIST_30") | Out-Null
		$historyForward = (Invoke-CheckedTmux $Platform $serverName @(
		    "display-message", "-p", "-t", "$session`:keyio",
		    "#{copy_cursor_line}")).Out.Trim()
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio", "cancel") |
		    Out-Null
		Add-Result $Results $Platform "copy-mode history search" `
		    ((Test-Contains $historyBackward "PARITY_HIST_7") -and
		    (Test-Contains $historyForward "PARITY_HIST_30")) `
		    "backward=$historyBackward;forward=$historyForward"

		$selectionCommand = if ($Platform -eq "windows") {
			'for /l %i in (1,1,5) do @echo PARITY_SELECT_%i'
		} else {
			"printf '%s\n' " + ((1..5 | ForEach-Object {
			    "PARITY_SELECT_$_"
			}) -join " ")
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    $selectionCommand, "Enter") | Out-Null
		Start-Sleep -Milliseconds 800
		Invoke-CheckedTmux $Platform $serverName @(
		    "copy-mode", "-t", "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-backward", "PARITY_SELECT_1") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "begin-selection") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-forward", "PARITY_SELECT_3") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "cursor-down") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "copy-selection-and-cancel") | Out-Null
		$selectionBuffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer")).Out
		Add-Result $Results $Platform "copy-mode multi-line selection" `
		    ((Test-Contains $selectionBuffer "PARITY_SELECT_1") -and
		    (Test-Contains $selectionBuffer "PARITY_SELECT_2") -and
		    (Test-Contains $selectionBuffer "PARITY_SELECT_3")) `
		    $selectionBuffer.Trim()

		$rectangleCommand = if ($Platform -eq "windows") {
			'for %i in (A B C) do @echo RCT_%i_98765'
		} else {
			"printf '%s\n' RCT_A_98765 RCT_B_98765 RCT_C_98765"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-t", "$session`:keyio",
		    $rectangleCommand, "Enter") | Out-Null
		Start-Sleep -Milliseconds 800
		Invoke-CheckedTmux $Platform $serverName @(
		    "copy-mode", "-t", "$session`:keyio") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "search-backward", "RCT_A") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "begin-selection") | Out-Null
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "rectangle-on") | Out-Null
		foreach ($step in 1..5) {
			Invoke-CheckedTmux $Platform $serverName @(
			    "send-keys", "-X", "-t", "$session`:keyio",
			    "cursor-right") | Out-Null
		}
		foreach ($step in 1..2) {
			Invoke-CheckedTmux $Platform $serverName @(
			    "send-keys", "-X", "-t", "$session`:keyio",
			    "cursor-down") | Out-Null
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "send-keys", "-X", "-t", "$session`:keyio",
		    "copy-selection-and-cancel") | Out-Null
		$rectangleBuffer = (Invoke-CheckedTmux $Platform $serverName @(
		    "show-buffer")).Out
		Add-Result $Results $Platform "copy-mode rectangle selection" `
		    ((Test-Contains $rectangleBuffer "RCT_A") -and
		    (Test-Contains $rectangleBuffer "RCT_B") -and
		    (Test-Contains $rectangleBuffer "RCT_C")) `
		    $rectangleBuffer.Trim()

		$ioCommand = if ($Platform -eq "windows") {
			"cmd.exe /d /c echo PARITY_PANE_IO_OK & timeout /t 30 /nobreak >NUL"
		} else {
			"sh -lc 'echo PARITY_PANE_IO_OK; sleep 30'"
		}
		Invoke-CheckedTmux $Platform $serverName @(
		    "new-window", "-d", "-t", $session, "-n", "io",
		    $ioCommand) | Out-Null
		Start-Sleep -Milliseconds 1000
		$capture = (Invoke-CheckedTmux $Platform $serverName @(
		    "capture-pane", "-p", "-t", "$session`:io")).Out
		Add-Result $Results $Platform "pane output capture" `
		    (Test-Contains $capture "PARITY_PANE_IO_OK") `
		    $capture.Trim()

		$paneFormats = (Invoke-CheckedTmux $Platform $serverName @(
		    "list-panes", "-a", "-F",
		    "#{session_name}:#{window_index}:#{pane_index}:#{pane_width}x#{pane_height}")).Out
		Add-Result $Results $Platform "pane format list" `
		    ((Test-Contains $paneFormats "$session`:0:0:") -and
		    (Test-Contains $paneFormats "x")) $paneFormats.Trim()

		$control = Invoke-PlatformTmux $Platform $serverName @(
		    "-C", "list-sessions", "-F",
		    "#{session_name}:#{session_windows}") -AllowFailure
		Add-Result $Results $Platform "control mode command client" `
		    ($control.ExitCode -eq 0 -and
		    (Test-Contains $control.Out "%begin") -and
		    (Test-Contains $control.Out "$session`:")) `
		    $control.Out.Trim()

		$wait = Invoke-PlatformTmux $Platform $serverName @(
		    "wait-for", "-L", "parity-lock") -AllowFailure
		$unlock = Invoke-PlatformTmux $Platform $serverName @(
		    "wait-for", "-U", "parity-lock") -AllowFailure
		Add-Result $Results $Platform "wait-for lock/unlock" `
		    ($wait.ExitCode -eq 0 -and $unlock.ExitCode -eq 0) `
		    "lock=$($wait.ExitCode);unlock=$($unlock.ExitCode)"

		Add-Result $Results $Platform "version probe" $true $version
	} finally {
		try {
			Invoke-PlatformTmux $Platform $serverName @("kill-server") `
			    -AllowFailure | Out-Null
		} catch {
		}
	}
}

$results = [System.Collections.Generic.List[object]]::new()
Run-PlatformCases "windows" $results
Run-PlatformCases "linux" $results

$requiredCategories = @(
    "sessions",
    "windows",
    "panes",
    "buffers",
    "options",
    "environment",
    "paths",
    "formats",
    "configuration",
    "key-bindings",
    "commands",
    "copy-mode",
    "hooks",
    "control-mode"
)
$categoryCoverage = @(Get-CategoryCoverage $results.ToArray() `
    $requiredCategories)
$missingCategories = @($categoryCoverage | Where-Object {
    -not $_.Covered
})
$failed = @($results | Where-Object { -not $_.Passed })
$status = if ($failed.Count -eq 0 -and $missingCategories.Count -eq 0) {
	"passed"
} else {
	"failed"
}
$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Status = $status
	WindowsTmux = $WindowsTmux
	Wsl = $Wsl
	Passed = @($results | Where-Object { $_.Passed }).Count
	Failed = $failed.Count
	RequiredCategories = $requiredCategories
	CategoryCoverage = $categoryCoverage
	Results = @($results.ToArray())
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "linux_behavior_parity=$Output"
Write-Host "status=$status"
Write-Host "failed=$($failed.Count)"
if ($missingCategories.Count -gt 0) {
	Write-Host ("missing_categories={0}" -f `
	    (($missingCategories | ForEach-Object { $_.Category }) -join ","))
}
if ($failed.Count -gt 0 -or $missingCategories.Count -gt 0) {
	exit 1
}
