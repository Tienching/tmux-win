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
	$Output = Join-Path $Root "dist\linux-parity-matrix.json"
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

function Invoke-WindowsTmux([string]$ServerName, [string[]]$Arguments,
    [switch]$AllowFailure) {
	$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$argumentString = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	return Invoke-CapturedProcess $WindowsTmux $argumentString `
	    -AllowFailure:$AllowFailure
}

function Invoke-LinuxTmux([string]$ServerName, [string[]]$Arguments,
    [switch]$AllowFailure) {
	$tmuxArguments = @("-L", $ServerName, "-f", "/dev/null") +
	    $Arguments
	$shellCommand = "tmux " + (($tmuxArguments | ForEach-Object {
	    ConvertTo-ShSingleQuoted $_
	}) -join " ")
	$wslArguments = "sh -lc " + (ConvertTo-WindowsArgument $shellCommand)
	return Invoke-CapturedProcess $Wsl $wslArguments `
	    -AllowFailure:$AllowFailure
}

function Split-Lines([string]$Text) {
	return @($Text -split "`r?`n" | Where-Object {
	    -not [string]::IsNullOrWhiteSpace($_)
	})
}

function Get-CommandNames([string[]]$Lines) {
	return @($Lines | ForEach-Object {
	    ($_ -split '[ (]', 2)[0]
	} | Sort-Object -Unique)
}

function Get-OptionNames([string[]]$Lines) {
	return @($Lines | ForEach-Object {
	    if ($_ -match '^([^\s\[]+)') {
		    $Matches[1]
	    }
	} | Sort-Object -Unique)
}

function Get-KeyTables([string[]]$Lines) {
	return @($Lines | ForEach-Object {
	    if ($_ -match '^\s*bind-key\s+-T\s+(\S+)') {
		    $Matches[1]
	    }
	} | Sort-Object -Unique)
}

function Compare-NameSet([string]$Name, [string[]]$Linux,
    [string[]]$Windows) {
	$linuxSet = [System.Collections.Generic.HashSet[string]]::new(
	    [System.StringComparer]::Ordinal)
	$windowsSet = [System.Collections.Generic.HashSet[string]]::new(
	    [System.StringComparer]::Ordinal)
	foreach ($item in $Linux) {
		[void]$linuxSet.Add($item)
	}
	foreach ($item in $Windows) {
		[void]$windowsSet.Add($item)
	}
	$missingOnWindows = @($Linux | Where-Object {
	    -not $windowsSet.Contains($_)
	})
	$windowsOnly = @($Windows | Where-Object {
	    -not $linuxSet.Contains($_)
	})
	return [pscustomobject]@{
		Name = $Name
		LinuxCount = $Linux.Count
		WindowsCount = $Windows.Count
		MissingOnWindows = $missingOnWindows
		WindowsOnly = $windowsOnly
	}
}

function Get-Surface([string]$Platform, [string]$ServerName) {
	function Invoke-SurfaceTmux([string[]]$TmuxArguments,
	    [switch]$AllowFailure) {
		if ($Platform -eq "windows") {
			return Invoke-WindowsTmux $ServerName $TmuxArguments `
			    -AllowFailure:$AllowFailure
		}
		return Invoke-LinuxTmux $ServerName $TmuxArguments `
		    -AllowFailure:$AllowFailure
	}
	$sessionCommand = if ($Platform -eq "windows") { "cmd.exe" } else { "sh" }

	try {
		$version = (Invoke-SurfaceTmux -TmuxArguments @("-V")).Out.Trim()
		Invoke-SurfaceTmux -TmuxArguments @(
		    "new-session", "-d", "-s", "surface", $sessionCommand) |
		    Out-Null
		$commandLines = Split-Lines (Invoke-SurfaceTmux `
		    -TmuxArguments @("list-commands")).Out
		$globalOptionLines = Split-Lines (Invoke-SurfaceTmux `
		    -TmuxArguments @("show-options", "-g")).Out
		$serverOptionLines = Split-Lines (Invoke-SurfaceTmux `
		    -TmuxArguments @("show-options", "-s")).Out
		$windowOptionLines = Split-Lines (Invoke-SurfaceTmux `
		    -TmuxArguments @("show-window-options", "-g")).Out
		$keyLines = Split-Lines (Invoke-SurfaceTmux `
		    -TmuxArguments @("list-keys")).Out

		return [pscustomobject]@{
			Platform = $Platform
			Version = $version
			Commands = Get-CommandNames $commandLines
			GlobalOptions = Get-OptionNames $globalOptionLines
			ServerOptions = Get-OptionNames $serverOptionLines
			WindowOptions = Get-OptionNames $windowOptionLines
			KeyTables = Get-KeyTables $keyLines
			KeyBindingCount = $keyLines.Count
		}
	} finally {
		try {
			Invoke-SurfaceTmux -TmuxArguments @("kill-server") `
			    -AllowFailure | Out-Null
		} catch {
		}
	}
}

$windowsServer = "win-surface-" + [Guid]::NewGuid().ToString("N")
$linuxServer = "linux-surface-" + [Guid]::NewGuid().ToString("N")
$windows = Get-Surface "windows" $windowsServer
$linux = Get-Surface "linux" $linuxServer

$comparisons = @(
    Compare-NameSet "commands" $linux.Commands $windows.Commands
    Compare-NameSet "global options" $linux.GlobalOptions `
	$windows.GlobalOptions
    Compare-NameSet "server options" $linux.ServerOptions `
	$windows.ServerOptions
    Compare-NameSet "window options" $linux.WindowOptions `
	$windows.WindowOptions
    Compare-NameSet "key tables" $linux.KeyTables $windows.KeyTables
)
$missingTotal = 0
foreach ($comparison in $comparisons) {
	$missingTotal += @($comparison.MissingOnWindows).Count
}

$status = if ($missingTotal -eq 0) { "passed" } else { "failed" }
$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Status = $status
	WindowsTmux = $WindowsTmux
	Wsl = $Wsl
	WindowsVersion = $windows.Version
	LinuxVersion = $linux.Version
	MissingLinuxSurfaceItemsOnWindows = $missingTotal
	Comparisons = $comparisons
	WindowsKeyBindingCount = $windows.KeyBindingCount
	LinuxKeyBindingCount = $linux.KeyBindingCount
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summary | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "linux_parity_matrix=$Output"
Write-Host "status=$status"
Write-Host "missing_linux_surface_items_on_windows=$missingTotal"
