param(
	[string]$ReleaseSummary = "",
	[string]$CommandSurfaceSummary = "",
	[string]$MsixSummary = "",
	[string]$VisualTerminalSummary = "",
	[string]$SigningSummary = "",
	[string]$IpcBoundarySummary = "",
	[string]$LinuxParitySummary = "",
	[string]$LinuxBehaviorSummary = "",
	[string]$HostedCiSummary = "",
	[string]$SourceStateSummary = "",
	[string]$HostedCiRunUrl = "",
	[string]$Output = "",
	[switch]$RequireComplete
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

function Add-Evidence([System.Collections.Generic.List[object]]$List,
    [string]$Name, [bool]$Covered, [string]$Detail) {
	$List.Add([pscustomobject]@{
	    Name = $Name
	    Covered = $Covered
	    Detail = $Detail
	})
}

function Add-Missing([System.Collections.Generic.List[object]]$List,
    [string]$Name, [string]$Detail) {
	$List.Add([pscustomobject]@{
	    Name = $Name
	    Detail = $Detail
	})
}

function Add-Checklist([System.Collections.Generic.List[object]]$List,
    [string]$Requirement, [bool]$Covered, [string[]]$EvidenceNames,
    [string]$Gap) {
	$List.Add([pscustomobject]@{
	    Requirement = $Requirement
	    Covered = $Covered
	    Evidence = $EvidenceNames
	    Gap = $Gap
	})
}

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Dist "completion-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

$ReleaseSummary = Resolve-InputPath $ReleaseSummary "release-check.json"
$CommandSurfaceSummary = Resolve-InputPath $CommandSurfaceSummary `
    "command-surface.json"
$MsixSummary = Resolve-InputPath $MsixSummary "tmux-win32.msix.json"

$release = Get-Content -LiteralPath $ReleaseSummary -Raw | ConvertFrom-Json
$surface = Get-Content -LiteralPath $CommandSurfaceSummary -Raw |
    ConvertFrom-Json
$msix = Get-Content -LiteralPath $MsixSummary -Raw | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($VisualTerminalSummary)) {
	if ($release.PSObject.Properties.Name -contains "VisualTerminalSummary") {
		$VisualTerminalSummary = $release.VisualTerminalSummary
	}
}
if (-not [string]::IsNullOrWhiteSpace($VisualTerminalSummary)) {
	if (-not [System.IO.Path]::IsPathRooted($VisualTerminalSummary)) {
		$VisualTerminalSummary = Join-Path (Get-Location) `
		    $VisualTerminalSummary
	}
	$VisualTerminalSummary =
	    [System.IO.Path]::GetFullPath($VisualTerminalSummary)
}
if (-not [string]::IsNullOrWhiteSpace($SigningSummary)) {
	if (-not [System.IO.Path]::IsPathRooted($SigningSummary)) {
		$SigningSummary = Join-Path (Get-Location) `
		    $SigningSummary
	}
	$SigningSummary =
	    [System.IO.Path]::GetFullPath($SigningSummary)
	if (-not (Test-Path -LiteralPath $SigningSummary)) {
		throw "signing summary not found: $SigningSummary"
	}
}
if (-not [string]::IsNullOrWhiteSpace($IpcBoundarySummary)) {
	if (-not [System.IO.Path]::IsPathRooted($IpcBoundarySummary)) {
		$IpcBoundarySummary = Join-Path (Get-Location) `
		    $IpcBoundarySummary
	}
	$IpcBoundarySummary =
	    [System.IO.Path]::GetFullPath($IpcBoundarySummary)
	if (-not (Test-Path -LiteralPath $IpcBoundarySummary)) {
		throw "IPC boundary summary not found: $IpcBoundarySummary"
	}
}
if (-not [string]::IsNullOrWhiteSpace($LinuxParitySummary)) {
	if (-not [System.IO.Path]::IsPathRooted($LinuxParitySummary)) {
		$LinuxParitySummary = Join-Path (Get-Location) `
		    $LinuxParitySummary
	}
	$LinuxParitySummary =
	    [System.IO.Path]::GetFullPath($LinuxParitySummary)
	if (-not (Test-Path -LiteralPath $LinuxParitySummary)) {
		throw "Linux parity summary not found: $LinuxParitySummary"
	}
}
if (-not [string]::IsNullOrWhiteSpace($LinuxBehaviorSummary)) {
	if (-not [System.IO.Path]::IsPathRooted($LinuxBehaviorSummary)) {
		$LinuxBehaviorSummary = Join-Path (Get-Location) `
		    $LinuxBehaviorSummary
	}
	$LinuxBehaviorSummary =
	    [System.IO.Path]::GetFullPath($LinuxBehaviorSummary)
	if (-not (Test-Path -LiteralPath $LinuxBehaviorSummary)) {
		throw "Linux behavior summary not found: $LinuxBehaviorSummary"
	}
}
if (-not [string]::IsNullOrWhiteSpace($HostedCiSummary)) {
	if (-not [System.IO.Path]::IsPathRooted($HostedCiSummary)) {
		$HostedCiSummary = Join-Path (Get-Location) `
		    $HostedCiSummary
	}
	$HostedCiSummary =
	    [System.IO.Path]::GetFullPath($HostedCiSummary)
	if (-not (Test-Path -LiteralPath $HostedCiSummary)) {
		throw "hosted CI summary not found: $HostedCiSummary"
	}
}
if (-not [string]::IsNullOrWhiteSpace($SourceStateSummary)) {
	if (-not [System.IO.Path]::IsPathRooted($SourceStateSummary)) {
		$SourceStateSummary = Join-Path (Get-Location) `
		    $SourceStateSummary
	}
	$SourceStateSummary =
	    [System.IO.Path]::GetFullPath($SourceStateSummary)
	if (-not (Test-Path -LiteralPath $SourceStateSummary)) {
		throw "source state summary not found: $SourceStateSummary"
	}
}

$evidence = [System.Collections.Generic.List[object]]::new()
$missing = [System.Collections.Generic.List[object]]::new()

$objective = "Native Windows tmux with the same practical feature surface as the Linux build"
$criteria = @(
    "Native Windows build and portable artifact",
    "Core tmux server/session/window/pane/job/client behavior",
    "Interactive attached-client rendering and input on Windows Terminal",
    "Command/option/key-binding surface parity audit",
    "Packaging, install/uninstall, MSIX artifact, and release notes",
    "Production-signable artifacts and trusted signing",
    "Hosted CI evidence for a clean Windows runner",
    "Artifacts are traceable to a clean committed source state",
    "Documented remaining Linux parity gaps"
)

$steps = @{}
foreach ($step in @($release.Steps)) {
	$steps[$step.Name] = $step
}

$requiredPassedSteps = @(
    "build",
    "package-smoke",
    "zip-sha256",
    "manifest-hashes",
    "command-surface",
    "msix-package",
    "respawn-stress",
    "ipc-acl-stress",
    "job-stress",
    "client-lifecycle-stress",
    "signal-matrix-stress",
    "config-parser-stress",
    "zip-install-uninstall",
    "stress",
    "soak",
    "console-soak"
)
foreach ($name in $requiredPassedSteps) {
	$covered = $steps.ContainsKey($name) -and
	    $steps[$name].Status -eq "passed"
	Add-Evidence $evidence "release step: $name" $covered `
	    $(if ($covered) { $steps[$name].Detail } else { "not passed" })
	if (-not $covered) {
		Add-Missing $missing "release step: $name" `
		    "Required local gate step did not pass in release summary."
	}
}

$releaseGateMinimums = @(
    [pscustomobject]@{
	Name = "respawn iterations"; Property = "RespawnIterations"; Minimum = 20
    },
    [pscustomobject]@{
	Name = "IPC ACL iterations"; Property = "IpcAclIterations"; Minimum = 3
    },
    [pscustomobject]@{
	Name = "job stress iterations"; Property = "JobStressIterations"; Minimum = 10
    },
    [pscustomobject]@{
	Name = "client stress iterations"; Property = "ClientStressIterations"; Minimum = 5
    },
    [pscustomobject]@{
	Name = "signal matrix iterations"; Property = "SignalMatrixIterations"; Minimum = 3
    },
    [pscustomobject]@{
	Name = "packaged stress iterations"; Property = "StressIterations"; Minimum = 1
    },
    [pscustomobject]@{
	Name = "mixed soak seconds"; Property = "SoakSeconds"; Minimum = 10
    },
    [pscustomobject]@{
	Name = "console soak seconds"; Property = "ConsoleSoakSeconds"; Minimum = 10
    },
    [pscustomobject]@{
	Name = "console reattach cycles"; Property = "ConsoleReattachCycles"; Minimum = 2
    }
)
$releaseGateStrengthDetails = [System.Collections.Generic.List[string]]::new()
$releaseGateStrengthCovered = $true
foreach ($minimum in $releaseGateMinimums) {
	$value = 0
	if ($release.PSObject.Properties.Name -contains $minimum.Property) {
		$value = [int]$release.($minimum.Property)
	}
	if ($value -lt [int]$minimum.Minimum) {
		$releaseGateStrengthCovered = $false
	}
	$releaseGateStrengthDetails.Add(("{0}={1}/{2}" -f `
	    $minimum.Name, $value, $minimum.Minimum))
}
$configStressCovered = $false
if ($release.PSObject.Properties.Name -contains "RunConfigStress") {
	$configStressCovered = [bool]$release.RunConfigStress
}
if (-not $configStressCovered) {
	$releaseGateStrengthCovered = $false
}
$releaseGateStrengthDetails.Add("config parser stress=$configStressCovered")
Add-Evidence $evidence "release gate stress thresholds" `
    $releaseGateStrengthCovered `
    ($releaseGateStrengthDetails.ToArray() -join ";")
if (-not $releaseGateStrengthCovered) {
	Add-Missing $missing "release gate stress thresholds" `
	    ($releaseGateStrengthDetails.ToArray() -join ";")
}

$visualCovered = $false
if ($steps.ContainsKey("visual-terminal-verify") -and
    $steps["visual-terminal-verify"].Status -eq "passed" -and
    -not [string]::IsNullOrWhiteSpace($VisualTerminalSummary) -and
    (Test-Path -LiteralPath $VisualTerminalSummary)) {
	$visualText = Get-Content -LiteralPath $VisualTerminalSummary -Raw
	$visualCovered = $visualText -like "*ok=True*" -and
	    $visualText -like "*TMUX_VISUAL_OK_*"
}
Add-Evidence $evidence "visible Windows Terminal attach" $visualCovered `
    $(if ($visualCovered) { $VisualTerminalSummary } else {
	"missing or failed UIA visible-terminal result"
    })
if (-not $visualCovered) {
	Add-Missing $missing "visible Windows Terminal attach" `
	    "UIA-visible attached-client output is not covered by current evidence."
}

$surfaceCovered =
    [int]$surface.CommandCount -ge 90 -and
    [int]$surface.GlobalOptionCount -ge 60 -and
    [int]$surface.ServerOptionCount -ge 25 -and
    [int]$surface.WindowOptionCount -ge 70 -and
    [int]$surface.KeyBindingCount -ge 250
Add-Evidence $evidence "command surface counts" $surfaceCovered `
    ("commands={0};global={1};server={2};window={3};keys={4}" -f `
    $surface.CommandCount, $surface.GlobalOptionCount,
    $surface.ServerOptionCount, $surface.WindowOptionCount,
    $surface.KeyBindingCount)
if (-not $surfaceCovered) {
	Add-Missing $missing "command surface counts" `
	    "Command/option/key-binding counts are below the current baseline."
}

$msixSigned = $false
if ($msix.PSObject.Properties.Name -contains "Signed") {
	$msixSigned = [bool]$msix.Signed
}
Add-Evidence $evidence "MSIX artifact" $true `
    ("sha256={0};signed={1}" -f $msix.SHA256, $msixSigned)
$trustedSigningCovered = $msixSigned
$trustedSigningDetail = $(if ($msixSigned) {
    "MSIX summary reports signed artifact."
} else {
    "MSIX summary reports unsigned artifact."
})
if (-not [string]::IsNullOrWhiteSpace($SigningSummary)) {
	$signing = Get-Content -LiteralPath $SigningSummary -Raw |
	    ConvertFrom-Json
	$trustedSigningCovered = $signing.Status -eq "trusted"
	$trustedSigningDetail = ("status={0};authenticode={1};source={2}" -f `
	    $signing.Status, $signing.AuthenticodeStatus, $SigningSummary)
	Add-Evidence $evidence "production signing audit" `
	    $trustedSigningCovered $trustedSigningDetail
}
if (-not $trustedSigningCovered) {
	Add-Missing $missing "production trusted signing" `
	    $(if ([string]::IsNullOrWhiteSpace($trustedSigningDetail)) {
		"MSIX exists but is unsigned; production artifacts require trusted signing."
	    } else { $trustedSigningDetail })
}

$boundaryLoaded = $false
$boundaryOtherUserPassed = $false
$boundarySystemPassed = $false
if (-not [string]::IsNullOrWhiteSpace($IpcBoundarySummary)) {
	$boundary = Get-Content -LiteralPath $IpcBoundarySummary -Raw |
	    ConvertFrom-Json
	$boundaryLoaded = $true
	$boundaryFailed = @($boundary.Checks | Where-Object {
	    $_.Status -eq "failed"
	})
	$otherUser = @($boundary.Checks | Where-Object {
	    $_.Name -eq "other-user endpoint read"
	} | Select-Object -First 1)
	$systemTask = @($boundary.Checks | Where-Object {
	    $_.Name -eq "SYSTEM scheduled-task endpoint read"
	} | Select-Object -First 1)
	$boundaryOtherUserPassed =
	    $otherUser.Count -gt 0 -and $otherUser[0].Status -eq "passed"
	$boundarySystemPassed =
	    $systemTask.Count -gt 0 -and $systemTask[0].Status -eq "passed"
	Add-Evidence $evidence "IPC boundary audit" `
	    ($boundaryFailed.Count -eq 0) `
	    ("status={0};failed={1};source={2}" -f $boundary.Status,
	    $boundaryFailed.Count, $IpcBoundarySummary)
	Add-Evidence $evidence "SYSTEM/service endpoint denial" `
	    $boundarySystemPassed `
	    $(if ($boundarySystemPassed) { $systemTask[0].Detail } else {
		"not covered"
	    })
	Add-Evidence $evidence "other-user endpoint denial" `
	    $boundaryOtherUserPassed `
	    $(if ($boundaryOtherUserPassed) { $otherUser[0].Detail } else {
		"not covered"
	    })
	if ($boundaryFailed.Count -gt 0) {
		Add-Missing $missing "Windows IPC boundary audit failures" `
		    "One or more IPC boundary checks failed; inspect $IpcBoundarySummary."
	}
}

$hostedCiCovered = $false
$hostedCiDetail = ""
$hostedCiTargetHeadSha = ""
if (-not [string]::IsNullOrWhiteSpace($HostedCiSummary)) {
	$hostedCi = Get-Content -LiteralPath $HostedCiSummary -Raw |
	    ConvertFrom-Json
	if ($hostedCi.Status -eq "passed" -and
	    $hostedCi.GreenRun -ne $null) {
		$targetHeadSha = ""
		if ($hostedCi.PSObject.Properties.Name -contains "HeadSha") {
			$targetHeadSha = [string]$hostedCi.HeadSha
		}
		$hostedCiTargetHeadSha = $targetHeadSha
		$greenHeadSha = ""
		if ($hostedCi.GreenRun.PSObject.Properties.Name -contains "HeadSha") {
			$greenHeadSha = [string]$hostedCi.GreenRun.HeadSha
		}
		if (-not [string]::IsNullOrWhiteSpace($targetHeadSha) -and
		    $greenHeadSha -ne $targetHeadSha) {
			$hostedCiDetail = ("status=failed;detail=green run head SHA {0} does not match target {1};source={2}" -f `
			    $greenHeadSha, $targetHeadSha,
			    $HostedCiSummary)
		} else {
			$hostedCiCovered = $true
			$hostedCiDetail = $hostedCi.GreenRun.Url
			if (-not [string]::IsNullOrWhiteSpace($targetHeadSha)) {
				$hostedCiDetail = "$hostedCiDetail;head_sha=$targetHeadSha"
			}
		}
	} else {
		if ($hostedCi.PSObject.Properties.Name -contains "HeadSha") {
			$hostedCiTargetHeadSha = [string]$hostedCi.HeadSha
		}
		$hostedCiDetail = ("status={0};detail={1};source={2}" -f `
		    $hostedCi.Status, $hostedCi.Detail, $HostedCiSummary)
	}
}
if (-not [string]::IsNullOrWhiteSpace($HostedCiRunUrl)) {
	if ([string]::IsNullOrWhiteSpace($hostedCiDetail)) {
		$hostedCiDetail = "run_url=$HostedCiRunUrl;status=unverified"
	} else {
		$hostedCiDetail = "$hostedCiDetail;run_url=$HostedCiRunUrl"
	}
}
if ($hostedCiCovered) {
	Add-Evidence $evidence "hosted CI green run" $true $hostedCiDetail
} else {
	Add-Evidence $evidence "hosted CI green run" $false `
	    $(if ([string]::IsNullOrWhiteSpace($hostedCiDetail)) {
		"No hosted CI audit or run URL recorded."
	    } else { $hostedCiDetail })
	Add-Missing $missing "hosted CI green run" `
	    $(if ([string]::IsNullOrWhiteSpace($hostedCiDetail)) {
		"No hosted GitHub Actions run URL or green run evidence is recorded in this workspace."
	    } else { $hostedCiDetail })
}

$sourceStateCovered = $false
$sourceStateDetail = "No source state audit recorded."
$sourceHead = ""
if (-not [string]::IsNullOrWhiteSpace($SourceStateSummary)) {
	$sourceState = Get-Content -LiteralPath $SourceStateSummary -Raw |
	    ConvertFrom-Json
	$sourceDirty = $true
	if ($sourceState.PSObject.Properties.Name -contains "IsDirty") {
		$sourceDirty = [bool]$sourceState.IsDirty
	}
	if ($sourceState.PSObject.Properties.Name -contains "HeadSha") {
		$sourceHead = [string]$sourceState.HeadSha
	}
	$trackedChanged = 0
	if ($sourceState.PSObject.Properties.Name -contains
	    "TrackedChangedCount") {
		$trackedChanged = [int]$sourceState.TrackedChangedCount
	}
	$untrackedCount = 0
	if ($sourceState.PSObject.Properties.Name -contains
	    "UntrackedCount") {
		$untrackedCount = [int]$sourceState.UntrackedCount
	}
	$sourceFingerprint = ""
	if ($sourceState.PSObject.Properties.Name -contains
	    "SourceStateFingerprint") {
		$sourceFingerprint = [string]$sourceState.SourceStateFingerprint
	}
	$sourceStateCovered = -not $sourceDirty -and
	    -not [string]::IsNullOrWhiteSpace($sourceHead)
	$sourceStateDetail = ("head={0};dirty={1};tracked={2};untracked={3};fingerprint={4};source={5}" -f `
	    $sourceHead, $sourceDirty, $trackedChanged, $untrackedCount,
	    $sourceFingerprint, $SourceStateSummary)
}
Add-Evidence $evidence "clean committed source state" `
    $sourceStateCovered $sourceStateDetail
if (-not $sourceStateCovered) {
	Add-Missing $missing "clean committed source state" `
	    $sourceStateDetail
}
$sourceAndHostedHeadCovered = $false
$sourceAndHostedHeadDetail = "not covered"
if (-not [string]::IsNullOrWhiteSpace($sourceHead) -and
    -not [string]::IsNullOrWhiteSpace($hostedCiTargetHeadSha)) {
	$sourceAndHostedHeadCovered = $sourceHead -eq $hostedCiTargetHeadSha
	$sourceAndHostedHeadDetail = ("source_head={0};hosted_ci_head={1}" -f `
	    $sourceHead, $hostedCiTargetHeadSha)
}
Add-Evidence $evidence "source and hosted CI head match" `
    $sourceAndHostedHeadCovered $sourceAndHostedHeadDetail
if (-not $sourceAndHostedHeadCovered) {
	Add-Missing $missing "source and hosted CI head mismatch" `
	    $sourceAndHostedHeadDetail
}

$linuxSurfaceCovered = $false
$linuxBehaviorCovered = $false
$linuxBehaviorCategoryCovered = $false
$linuxBehaviorCategoryDetail = "not covered"
if (-not [string]::IsNullOrWhiteSpace($LinuxParitySummary)) {
	$linuxParity = Get-Content -LiteralPath $LinuxParitySummary -Raw |
	    ConvertFrom-Json
	$linuxSurfaceCovered = $linuxParity.Status -eq "passed" -and
	    [int]$linuxParity.MissingLinuxSurfaceItemsOnWindows -eq 0
	Add-Evidence $evidence "Linux command/option/key surface parity" `
	    $linuxSurfaceCovered `
	    ("windows={0};linux={1};missing={2};source={3}" -f `
	    $linuxParity.WindowsVersion, $linuxParity.LinuxVersion,
	    $linuxParity.MissingLinuxSurfaceItemsOnWindows,
	    $LinuxParitySummary)
	if (-not $linuxSurfaceCovered) {
		Add-Missing $missing "Linux command/option/key surface gaps" `
		    "The Linux parity matrix found Linux surface items missing on Windows."
	}
}
if (-not [string]::IsNullOrWhiteSpace($LinuxBehaviorSummary)) {
	$linuxBehavior = Get-Content -LiteralPath $LinuxBehaviorSummary -Raw |
	    ConvertFrom-Json
	$linuxBehaviorCovered = $linuxBehavior.Status -eq "passed" -and
	    [int]$linuxBehavior.Failed -eq 0
	Add-Evidence $evidence "Linux focused behavior parity" `
	    $linuxBehaviorCovered `
	    ("passed={0};failed={1};source={2}" -f `
	    $linuxBehavior.Passed, $linuxBehavior.Failed,
	    $LinuxBehaviorSummary)
	if (-not $linuxBehaviorCovered) {
		Add-Missing $missing "Linux focused behavior parity gaps" `
		    "The focused Linux/Windows behavior matrix has failing cases."
	}
	if ($linuxBehavior.PSObject.Properties.Name -contains
	    "CategoryCoverage") {
		$requiredCategories = @($linuxBehavior.RequiredCategories)
		$categoryCoverage = @($linuxBehavior.CategoryCoverage)
		$missingCategories =
		    [System.Collections.Generic.List[string]]::new()
		foreach ($category in $requiredCategories) {
			$entry = @($categoryCoverage | Where-Object {
			    $_.Category -eq $category -and $_.Covered
			})
			if ($entry.Count -eq 0) {
				$missingCategories.Add([string]$category)
			}
		}
		$linuxBehaviorCategoryCovered =
		    $linuxBehaviorCovered -and $missingCategories.Count -eq 0
		$linuxBehaviorCategoryDetail = ("categories={0};missing={1}" -f `
		    ($requiredCategories -join ","),
		    ($missingCategories.ToArray() -join ","))
		Add-Evidence $evidence "Linux behavior category coverage" `
		    $linuxBehaviorCategoryCovered `
		    ("{0};source={1}" -f $linuxBehaviorCategoryDetail,
		    $LinuxBehaviorSummary)
		if (-not $linuxBehaviorCategoryCovered) {
			Add-Missing $missing `
			    "Linux behavior category coverage gaps" `
			    $linuxBehaviorCategoryDetail
		}
	} else {
		Add-Evidence $evidence "Linux behavior category coverage" `
		    $false "CategoryCoverage is not present."
		Add-Missing $missing "Linux behavior category coverage gaps" `
		    "The Linux behavior summary does not include CategoryCoverage."
	}
}
if (-not ($linuxSurfaceCovered -and $linuxBehaviorCovered -and
    $linuxBehaviorCategoryCovered)) {
	Add-Missing $missing "Linux behavior parity matrix gaps" `
	    "Linux surface, focused behavior, or category coverage evidence is incomplete."
}
if (-not $boundaryLoaded) {
	Add-Missing $missing "Windows ACL/domain/service edge cases" `
	    "No IPC boundary audit summary is recorded."
} else {
	if (-not $boundarySystemPassed) {
		Add-Missing $missing "Windows service/session boundary" `
		    "SYSTEM/service endpoint denial is not covered."
	}
	if (-not $boundaryOtherUserPassed) {
		Add-Missing $missing "Windows other-user/domain account boundary" `
		    "Run windows/ipc-boundary-audit.ps1 with -OtherUserCredential, or with -CreateTemporaryLocalUser from an elevated PowerShell, to verify a second account cannot read the endpoint token."
	}
}

$checklist = [System.Collections.Generic.List[object]]::new()
$releaseNotesPath = Join-Path $Dist "windows-release-notes.md"
$releaseNotesCovered = Test-Path -LiteralPath $releaseNotesPath
$coreBehaviorCovered = @(
    "respawn-stress",
    "ipc-acl-stress",
    "job-stress",
    "client-lifecycle-stress",
    "signal-matrix-stress",
    "config-parser-stress",
    "stress",
    "soak",
    "console-soak"
) | ForEach-Object {
    $steps.ContainsKey($_) -and $steps[$_].Status -eq "passed"
}
$coreBehaviorCovered = -not ($coreBehaviorCovered -contains $false) -and
    $releaseGateStrengthCovered
$packageCovered =
    $steps.ContainsKey("build") -and
    $steps["build"].Status -eq "passed" -and
    $steps.ContainsKey("package-smoke") -and
    $steps["package-smoke"].Status -eq "passed" -and
    $steps.ContainsKey("zip-sha256") -and
    $steps["zip-sha256"].Status -eq "passed" -and
    $steps.ContainsKey("manifest-hashes") -and
    $steps["manifest-hashes"].Status -eq "passed"
$packagingCovered =
    $packageCovered -and
    $steps.ContainsKey("msix-package") -and
    $steps["msix-package"].Status -eq "passed" -and
    $steps.ContainsKey("zip-install-uninstall") -and
    $steps["zip-install-uninstall"].Status -eq "passed" -and
    $releaseNotesCovered

Add-Checklist $checklist "Native Windows build and portable artifact" `
    $packageCovered @(
    "release step: build",
    "release step: package-smoke",
    "release step: zip-sha256",
    "release step: manifest-hashes"
) $(if ($packageCovered) { "" } else {
    "Package, zip, or manifest hash evidence is missing."
})
Add-Checklist $checklist `
    "Core server/session/window/pane/job/client behavior" `
    $coreBehaviorCovered @(
    "release step: respawn-stress",
    "release step: ipc-acl-stress",
    "release step: job-stress",
    "release step: client-lifecycle-stress",
    "release step: signal-matrix-stress",
    "release step: config-parser-stress",
    "release step: stress",
    "release step: soak",
    "release step: console-soak",
    "release gate stress thresholds"
) $(if ($coreBehaviorCovered) { "" } else {
    "One or more core behavior release steps are missing or below release gate thresholds."
})
Add-Checklist $checklist `
    "Interactive attached-client rendering and input on Windows Terminal" `
    $visualCovered @("visible Windows Terminal attach") `
    $(if ($visualCovered) { "" } else {
	"Visible Windows Terminal UIA evidence is missing."
    })
Add-Checklist $checklist "Command, option, and key-binding surface parity" `
    ($surfaceCovered -and $linuxSurfaceCovered) @(
    "command surface counts",
    "Linux command/option/key surface parity"
) $(if ($surfaceCovered -and $linuxSurfaceCovered) { "" } else {
    "Command-surface or Linux surface parity evidence is missing."
})
Add-Checklist $checklist `
    "Packaging, install/uninstall, MSIX artifact, and release notes" `
    $packagingCovered @(
    "release step: msix-package",
    "release step: zip-install-uninstall",
    "MSIX artifact"
) $(if ($packagingCovered) { "" } else {
    "Packaging, install/uninstall, MSIX, or release notes evidence is missing."
})
Add-Checklist $checklist "Production trusted signing" `
    $trustedSigningCovered @("MSIX artifact", "production signing audit") `
    $(if ($trustedSigningCovered) { "" } else {
    "MSIX exists but is not signed by a trusted production certificate."
})
Add-Checklist $checklist "Hosted CI green run" `
    $hostedCiCovered `
    @("hosted CI green run") `
    $(if ($hostedCiCovered) { "" } else {
	"No hosted GitHub Actions green run is recorded."
    })
Add-Checklist $checklist "Clean committed source state" `
    $sourceStateCovered `
    @("clean committed source state") `
    $(if ($sourceStateCovered) { "" } else {
	"Release artifacts are not tied to a clean committed source tree."
    })
Add-Checklist $checklist "Linux behavior parity evidence" `
    ($linuxSurfaceCovered -and $linuxBehaviorCovered -and
    $linuxBehaviorCategoryCovered) @(
    "Linux command/option/key surface parity",
    "Linux focused behavior parity",
    "Linux behavior category coverage"
) $(if ($linuxSurfaceCovered -and $linuxBehaviorCovered -and
    $linuxBehaviorCategoryCovered) {
    ""
} else {
    "Linux surface, focused behavior, or category coverage evidence is missing."
})
Add-Checklist $checklist "Windows IPC ACL/domain/service boundary evidence" `
    ($boundarySystemPassed -and $boundaryOtherUserPassed) @(
    "IPC boundary audit",
    "SYSTEM/service endpoint denial",
    "other-user endpoint denial"
) $(if ($boundarySystemPassed -and $boundaryOtherUserPassed) { "" } else {
    "Service/SYSTEM or real other-user/domain boundary evidence is incomplete."
})

$status = if ($missing.Count -eq 0) { "complete" } else { "not_complete" }
$audit = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Objective = $objective
	Status = $status
	SuccessCriteria = $criteria
	ReleaseSummary = $ReleaseSummary
	CommandSurfaceSummary = $CommandSurfaceSummary
	MsixSummary = $MsixSummary
	VisualTerminalSummary = $VisualTerminalSummary
	SigningSummary = $SigningSummary
	IpcBoundarySummary = $IpcBoundarySummary
	LinuxParitySummary = $LinuxParitySummary
	LinuxBehaviorSummary = $LinuxBehaviorSummary
	HostedCiSummary = $HostedCiSummary
	SourceStateSummary = $SourceStateSummary
	HostedCiRunUrl = $HostedCiRunUrl
	Checklist = @($checklist.ToArray())
	Evidence = @($evidence.ToArray())
	Missing = @($missing.ToArray())
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$audit | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "completion_audit=$Output"
Write-Host "status=$status"
if ($missing.Count -gt 0) {
	Write-Host ("missing={0}" -f $missing.Count)
}
if ($RequireComplete -and $missing.Count -gt 0) {
	exit 1
}
