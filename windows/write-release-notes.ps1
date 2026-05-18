param(
	[string]$Output = "",
	[string]$ReleaseSummary = "",
	[string]$CommandSurfaceSummary = "",
	[string]$MsixSummary = "",
	[string]$SigningSummary = "",
	[string]$IpcBoundarySummary = "",
	[string]$LinuxParitySummary = "",
	[string]$LinuxBehaviorSummary = "",
	[string]$HostedCiSummary = "",
	[string]$SourceStateSummary = "",
	[string]$CompletionAudit = "",
	[string]$ReleaseCheckCommand = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Dist = Join-Path $Root "dist"

function Resolve-InputPath([string]$Path, [string]$DefaultName) {
	if ([string]::IsNullOrWhiteSpace($Path)) {
		$Path = Join-Path $Dist $DefaultName
	} elseif (-not [System.IO.Path]::IsPathRooted($Path)) {
		$Path = Join-Path (Get-Location) $Path
	}
	$full = [System.IO.Path]::GetFullPath($Path)
	if (-not (Test-Path -LiteralPath $full)) {
		throw "input not found: $full"
	}
	return $full
}

function Resolve-OptionalInputPath([string]$Path, [string]$DefaultName) {
	if ([string]::IsNullOrWhiteSpace($Path)) {
		$Path = Join-Path $Dist $DefaultName
	} elseif (-not [System.IO.Path]::IsPathRooted($Path)) {
		$Path = Join-Path (Get-Location) $Path
	}
	$full = [System.IO.Path]::GetFullPath($Path)
	if (Test-Path -LiteralPath $full) {
		return $full
	}
	return ""
}

function Read-OptionalJson([string]$Path) {
	if ([string]::IsNullOrWhiteSpace($Path)) {
		return $null
	}
	return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Dist "windows-release-notes.md"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

$ReleaseSummary = Resolve-InputPath $ReleaseSummary "release-check.json"
$CommandSurfaceSummary = Resolve-InputPath $CommandSurfaceSummary `
    "command-surface.json"
$MsixSummary = Resolve-InputPath $MsixSummary "tmux-win32.msix.json"
$SigningSummary = Resolve-OptionalInputPath $SigningSummary `
    "signing-audit.json"
$IpcBoundarySummary = Resolve-OptionalInputPath $IpcBoundarySummary `
    "ipc-boundary-audit.json"
$LinuxParitySummary = Resolve-OptionalInputPath $LinuxParitySummary `
    "linux-parity-matrix.json"
$LinuxBehaviorSummary = Resolve-OptionalInputPath $LinuxBehaviorSummary `
    "linux-behavior-parity.json"
$HostedCiSummary = Resolve-OptionalInputPath $HostedCiSummary `
    "hosted-ci-audit.json"
$SourceStateSummary = Resolve-OptionalInputPath $SourceStateSummary `
    "source-state-audit.json"
$CompletionAudit = Resolve-OptionalInputPath $CompletionAudit `
    "completion-audit.json"

$release = Get-Content -LiteralPath $ReleaseSummary -Raw | ConvertFrom-Json
$surface = Get-Content -LiteralPath $CommandSurfaceSummary -Raw |
    ConvertFrom-Json
$msix = Get-Content -LiteralPath $MsixSummary -Raw | ConvertFrom-Json
$signing = Read-OptionalJson $SigningSummary
$ipcBoundary = Read-OptionalJson $IpcBoundarySummary
$linuxParity = Read-OptionalJson $LinuxParitySummary
$linuxBehavior = Read-OptionalJson $LinuxBehaviorSummary
$hostedCi = Read-OptionalJson $HostedCiSummary
$sourceState = Read-OptionalJson $SourceStateSummary
$completion = Read-OptionalJson $CompletionAudit

if ([string]::IsNullOrWhiteSpace($ReleaseCheckCommand)) {
	$releaseArgs = [System.Collections.Generic.List[string]]::new()
	$buildStep = @($release.Steps | Where-Object {
	    $_.Name -eq "build"
	} | Select-Object -First 1)
	if ($buildStep.Count -gt 0 -and $buildStep[0].Status -eq "skipped") {
		$releaseArgs.Add("-SkipBuild")
	}
	foreach ($pair in @(
	    @("RespawnIterations", "-RespawnIterations"),
	    @("IpcAclIterations", "-IpcAclIterations"),
	    @("JobStressIterations", "-JobStressIterations"),
	    @("ClientStressIterations", "-ClientStressIterations"),
	    @("SignalMatrixIterations", "-SignalMatrixIterations"),
	    @("StressIterations", "-StressIterations"),
	    @("SoakSeconds", "-SoakSeconds"),
	    @("ConsoleSoakSeconds", "-ConsoleSoakSeconds"),
	    @("ConsoleReattachCycles", "-ConsoleReattachCycles")
	)) {
		if ($release.PSObject.Properties.Name -contains $pair[0]) {
			$releaseArgs.Add($pair[1])
			$releaseArgs.Add([string]$release.($pair[0]))
		}
	}
	if ($release.PSObject.Properties.Name -contains "RunConfigStress" -and
	    [bool]$release.RunConfigStress) {
		$releaseArgs.Add("-RunConfigStress")
	}
	if ($release.PSObject.Properties.Name -contains
	    "RunVisualTerminalVerify" -and
	    [bool]$release.RunVisualTerminalVerify) {
		$releaseArgs.Add("-RunVisualTerminalVerify")
	}
	if ($release.PSObject.Properties.Name -contains "BuildMsix" -and
	    [bool]$release.BuildMsix) {
		$releaseArgs.Add("-BuildMsix")
	}
	$ReleaseCheckCommand = ".\windows\release-check.ps1 " +
	    ($releaseArgs.ToArray() -join " ")
}

$defaultOptionNote = ""
if ($surface.PSObject.Properties.Name -contains "OptionDefaults") {
	$defaultOptionNote =
	    "`n" +
	    'Windows default option checks were also verified for ' +
	    '`default-shell`, `default-terminal`, `lock-command`, ' +
	    '`set-clipboard`, `exit-empty`, `mode-keys`, and `window-size`.'
}

$msixStatus = if ($msix.Signed) { "signed" } else { "unsigned" }
$respawnCoverage = ""
if ($release.PSObject.Properties.Name -contains "RespawnIterations" -and
    [int]$release.RespawnIterations -gt 0) {
	$respawnCoverage = "targeted respawn stress, "
}
$ipcAclCoverage = ""
if ($release.PSObject.Properties.Name -contains "IpcAclIterations" -and
    [int]$release.IpcAclIterations -gt 0) {
	$ipcAclCoverage = "IPC ACL/token stress, "
}
$jobCoverage = ""
if ($release.PSObject.Properties.Name -contains "JobStressIterations" -and
    [int]$release.JobStressIterations -gt 0) {
	$jobCoverage = "job stdout/stderr and background stress, "
}
$clientCoverage = ""
if ($release.PSObject.Properties.Name -contains "ClientStressIterations" -and
    [int]$release.ClientStressIterations -gt 0) {
	$clientCoverage = "multi-client lifecycle stress, "
}
$signalCoverage = ""
if ($release.PSObject.Properties.Name -contains "SignalMatrixIterations" -and
    [int]$release.SignalMatrixIterations -gt 0) {
	$signalCoverage = "signal matrix stress, "
}
$configCoverage = ""
if ($release.PSObject.Properties.Name -contains "RunConfigStress" -and
    [bool]$release.RunConfigStress) {
	$configCoverage = "config parser stress, "
}
$visualCoverage = ""
if ($release.PSObject.Properties.Name -contains "RunVisualTerminalVerify" -and
    [bool]$release.RunVisualTerminalVerify) {
	$visualCoverage = "visible Windows Terminal UI verification, "
}

$evidenceRows = [System.Collections.Generic.List[string]]::new()
if ($null -ne $signing) {
	$authenticode = $(if ($signing.PSObject.Properties.Name -contains
	    "AuthenticodeStatus") { $signing.AuthenticodeStatus } else { "" })
	$evidenceRows.Add("| Signing audit | $($signing.Status); Authenticode=$authenticode |")
}
if ($null -ne $ipcBoundary) {
	$failed = @($ipcBoundary.Checks | Where-Object { $_.Status -eq "failed" })
	$evidenceRows.Add("| IPC boundary audit | $($ipcBoundary.Status); failed=$($failed.Count) |")
}
if ($null -ne $linuxParity) {
	$defaultOptionMismatches = ""
	if ($linuxParity.PSObject.Properties.Name -contains
	    "DefaultOptionMismatches") {
		$defaultOptionMismatches =
		    "; default_mismatches=$($linuxParity.DefaultOptionMismatches)"
	}
	$evidenceRows.Add("| Linux surface parity | $($linuxParity.Status); missing=$($linuxParity.MissingLinuxSurfaceItemsOnWindows)$defaultOptionMismatches |")
}
if ($null -ne $linuxBehavior) {
	$categories = ""
	if ($linuxBehavior.PSObject.Properties.Name -contains
	    "RequiredCategories") {
		$categories = @($linuxBehavior.RequiredCategories) -join ","
	}
	$evidenceRows.Add("| Linux behavior parity | $($linuxBehavior.Status); passed=$($linuxBehavior.Passed); failed=$($linuxBehavior.Failed); categories=$categories |")
}
if ($null -ne $hostedCi) {
	$headSha = ""
	if ($hostedCi.PSObject.Properties.Name -contains "HeadSha") {
		$headSha = [string]$hostedCi.HeadSha
	}
	$evidenceRows.Add("| Hosted CI audit | $($hostedCi.Status); head=$headSha |")
}
if ($null -ne $sourceState) {
	$sourceStateStatus = $(if ([bool]$sourceState.IsDirty) {
	    "dirty"
	} else { "clean" })
	$fingerprint = ""
	if ($sourceState.PSObject.Properties.Name -contains
	    "SourceStateFingerprint") {
		$fingerprint = [string]$sourceState.SourceStateFingerprint
	}
	$evidenceRows.Add("| Source state audit | $sourceStateStatus; head=$($sourceState.HeadSha); tracked=$($sourceState.TrackedChangedCount); untracked=$($sourceState.UntrackedCount); fingerprint=$fingerprint |")
}
if ($null -ne $completion) {
	$missing = @($completion.Missing)
	$evidenceRows.Add("| Completion audit | $($completion.Status); missing=$($missing.Count) |")
}
$evidenceSection = ""
if ($evidenceRows.Count -gt 0) {
	$evidenceSection = @"

## Evidence Summary

| Evidence | Status |
| --- | --- |
$($evidenceRows.ToArray() -join "`n")

"@
}
$notes = @"
# Windows tmux artifacts

Version: $($release.Version)

## Artifacts

| Artifact | SHA256 |
| --- | --- |
| tmux-win32-portable.zip | $($release.ZipSha256) |
| tmux-win32.msix | $($msix.SHA256) |

MSIX signing status: $msixStatus

## Release Gate

~~~powershell
$ReleaseCheckCommand
~~~

The Windows release gate passed packaged smoke, artifact hash verification,
command-surface audit, ${respawnCoverage}${ipcAclCoverage}${jobCoverage}${clientCoverage}${signalCoverage}${configCoverage}portable zip
install/uninstall verification, MSIX packaging, ${visualCoverage}and configured stress/soak coverage.

## Command Surface

| Surface | Count |
| --- | ---: |
| Commands | $($surface.CommandCount) |
| Global options | $($surface.GlobalOptionCount) |
| Server options | $($surface.ServerOptionCount) |
| Window options | $($surface.WindowOptionCount) |
| Key bindings | $($surface.KeyBindingCount) |

$defaultOptionNote
$evidenceSection
## Release Status

These artifacts are not production-complete unless the completion audit reports
"complete" and the MSIX is signed with a trusted production certificate.

## Install Notes

The portable zip can be installed with windows/install-portable.ps1.

The MSIX must be signed with a trusted code-signing certificate before it is
published as a production installer.
"@

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
Set-Content -LiteralPath $Output -Encoding ascii -Value $notes
Write-Host "release_notes=$Output"
