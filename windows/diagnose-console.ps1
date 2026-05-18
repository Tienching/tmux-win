param(
	[string]$Tmux = "",
	[switch]$ResetDefault,
	[switch]$RunQuickVerify
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

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class TmuxConsoleDiagnostics
{
	const int STD_INPUT_HANDLE = -10;
	const int STD_OUTPUT_HANDLE = -11;
	const int STD_ERROR_HANDLE = -12;

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr GetStdHandle(int nStdHandle);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GetConsoleMode(IntPtr hConsoleHandle, out uint mode);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern uint GetFileType(IntPtr hFile);

	public static string Describe(string name)
	{
		int id = name == "stdin" ? STD_INPUT_HANDLE :
		    (name == "stdout" ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
		IntPtr handle = GetStdHandle(id);
		if (handle == IntPtr.Zero || handle == new IntPtr(-1))
			return name + ": handle=invalid";

		uint mode;
		bool console = GetConsoleMode(handle, out mode);
		uint error = console ? 0 : (uint)Marshal.GetLastWin32Error();
		uint type = GetFileType(handle);
		return name + ": handle=0x" + handle.ToInt64().ToString("x") +
		    " file_type=" + type.ToString() +
		    " console=" + console.ToString() +
		    " mode=0x" + mode.ToString("x") +
		    " error=" + error.ToString();
	}
}
"@

function Write-Section([string]$Name) {
	Write-Host ""
	Write-Host "== $Name =="
}

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

function Invoke-DiagnoseTmux([string[]]$Arguments, [int]$Timeout = 5) {
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($Arguments | ForEach-Object {
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
		return [pscustomobject]@{
			TimedOut = $true
			ExitCode = -1
			Out = ""
			Err = "timed out"
		}
	}
	$process.WaitForExit()
	return [pscustomobject]@{
		TimedOut = $false
		ExitCode = $process.ExitCode
		Out = $stdoutTask.Result
		Err = $stderrTask.Result
	}
}

$endpoint = Join-Path (Join-Path $env:LOCALAPPDATA "tmux") "default.endpoint"

Write-Section "tmux"
Write-Host "tmux=$Tmux"
$version = (& $Tmux -V 2>&1)
Write-Host "version=$($version -join ' ')"
Write-Host "version_exit=$LASTEXITCODE"

Write-Section "console"
[TmuxConsoleDiagnostics]::Describe("stdin")
[TmuxConsoleDiagnostics]::Describe("stdout")
[TmuxConsoleDiagnostics]::Describe("stderr")
Write-Host "TERM=$env:TERM"
Write-Host "WT_SESSION=$env:WT_SESSION"
Write-Host "TERM_PROGRAM=$env:TERM_PROGRAM"
Write-Host "ConEmuANSI=$env:ConEmuANSI"

Write-Section "default endpoint"
if (Test-Path -LiteralPath $endpoint) {
	$item = Get-Item -LiteralPath $endpoint
	Write-Host "endpoint=$endpoint"
	Write-Host "endpoint_exists=True"
	Write-Host "endpoint_last_write=$($item.LastWriteTime.ToString('o'))"
	Write-Host "endpoint_length=$($item.Length)"
} else {
	Write-Host "endpoint=$endpoint"
	Write-Host "endpoint_exists=False"
}

Write-Section "tmux processes"
$processes = @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
    Select-Object ProcessId, ParentProcessId, CommandLine)
if ($processes.Count -eq 0) {
	Write-Host "tmux_processes=0"
} else {
	$processes | Format-Table -AutoSize
}

Write-Section "default server state"
$sessions = Invoke-DiagnoseTmux @("list-sessions")
if ($sessions.ExitCode -eq 0) {
	Write-Host "sessions:"
	Write-Host $sessions.Out.TrimEnd()
	$clients = Invoke-DiagnoseTmux @("list-clients", "-F",
	    "#{client_name} session=#{client_session} control=#{client_control_mode} size=#{client_width}x#{client_height}")
	if ($clients.ExitCode -eq 0) {
		Write-Host "clients:"
		Write-Host $clients.Out.TrimEnd()
		if ($clients.Out -like "*session=*") {
			Write-Host ("diagnosis=attached_client_present; " +
			    "a bare tmux.exe is expected to take over the " +
			    "terminal until Ctrl-b then d detaches it")
		}
	} else {
		Write-Host "clients_error=$($clients.Err.Trim())"
	}
	$panes = Invoke-DiagnoseTmux @("list-panes", "-a", "-F",
	    "#{session_name}:#{window_index}.#{pane_index} cmd=#{pane_current_command} dead=#{pane_dead} path=#{pane_current_path}")
	if ($panes.ExitCode -eq 0) {
		Write-Host "panes:"
		Write-Host $panes.Out.TrimEnd()
	} else {
		Write-Host "panes_error=$($panes.Err.Trim())"
	}
} elseif ($sessions.TimedOut) {
	Write-Host "sessions_error=timed out connecting to default server"
} else {
	Write-Host "sessions_error=$($sessions.Err.Trim())"
}

if ($ResetDefault) {
	Write-Section "reset default"
	try {
		& $Tmux kill-server 2>$null | Out-Null
		Write-Host "kill_server=attempted"
	} catch {
		Write-Host "kill_server=failed_or_not_running"
	}
	if (Test-Path -LiteralPath $endpoint) {
		Remove-Item -LiteralPath $endpoint -Force
		Write-Host "endpoint_removed=True"
	} else {
		Write-Host "endpoint_removed=False"
	}
}

if ($RunQuickVerify) {
	Write-Section "quick verify"
	& (Join-Path $PSScriptRoot "verify-portable.ps1") -Tmux $Tmux
}

Write-Section "manual attach test"
Write-Host "Use a fresh socket name to avoid default endpoint state:"
Write-Host ".\tmux.exe -L manual -f NUL new-session -s test cmd.exe"
Write-Host "A successful attach takes over this console; the default pane may be cmd.exe."
Write-Host "If the prompt changes to D:\...\> and accepts input, tmux is attached, not hung."
Write-Host "Type: echo TMUX_INTERACTIVE_OK"
Write-Host "Detach with Ctrl-b then d."
Write-Host "PowerShell pane test:"
Write-Host ".\tmux.exe -L manual-ps -f NUL new-session -s test powershell.exe"
