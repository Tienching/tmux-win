param(
	[string]$Dist = "",
	[string]$Package = "",
	[string]$ZipPath = "",
	[string]$ReleaseSummary = "",
	[string]$CommandSurfaceSummary = "",
	[string]$MsixPath = "",
	[string]$MsixSummary = "",
	[string]$SigningSummary = "",
	[string]$CompletionAudit = "",
	[string]$IpcBoundarySummary = "",
	[string]$LinuxParitySummary = "",
	[string]$LinuxBehaviorSummary = "",
	[string]$HostedCiSummary = "",
	[string]$SourceStateSummary = "",
	[switch]$RequireMsix,
	[switch]$RequireSignedMsix,
	[switch]$RequireSigningAudit,
	[switch]$RequireCompletionAudit,
	[switch]$RequireCompletionComplete,
	[switch]$RequireIpcBoundaryAudit,
	[switch]$RequireLinuxParity,
	[switch]$RequireLinuxBehaviorParity,
	[switch]$RequireHostedCiAudit,
	[switch]$RequireHostedCiGreen,
	[switch]$RequireSourceStateAudit,
	[switch]$RequireProductionReady
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Dist)) {
	$Dist = Join-Path $Root "dist"
} elseif (-not [System.IO.Path]::IsPathRooted($Dist)) {
	$Dist = Join-Path (Get-Location) $Dist
}
$Dist = [System.IO.Path]::GetFullPath($Dist)

function Resolve-ArtifactPath([string]$Path, [string]$DefaultName,
    [switch]$MustExist) {
	if ([string]::IsNullOrWhiteSpace($Path)) {
		$Path = Join-Path $Dist $DefaultName
	} elseif (-not [System.IO.Path]::IsPathRooted($Path)) {
		$Path = Join-Path (Get-Location) $Path
	}
	$full = [System.IO.Path]::GetFullPath($Path)
	if ($MustExist -and -not (Test-Path -LiteralPath $full)) {
		throw "artifact not found: $full"
	}
	return $full
}

function Read-Sha256Sidecar([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) {
		throw "sha256 sidecar not found: $Path"
	}
	$content = (Get-Content -LiteralPath $Path -Raw).Trim()
	if ($content -notmatch "^([0-9a-fA-F]{64})\s+(.+)$") {
		throw "invalid sha256 sidecar: $Path"
	}
	return $Matches[1].ToLowerInvariant()
}

function Get-Sha256([string]$Path) {
	return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).
	    Hash.ToLowerInvariant()
}

function Assert-Equal([string]$Name, [string]$Expected, [string]$Actual) {
	if ($Expected -ne $Actual) {
		throw "$Name mismatch: expected $Expected got $Actual"
	}
}

$Package = Resolve-ArtifactPath $Package "tmux-win32-portable" -MustExist
$ZipPath = Resolve-ArtifactPath $ZipPath "tmux-win32-portable.zip" -MustExist
$ReleaseSummary = Resolve-ArtifactPath $ReleaseSummary "release-check.json" `
    -MustExist
$CommandSurfaceSummary = Resolve-ArtifactPath $CommandSurfaceSummary `
    "command-surface.json" -MustExist
$MsixPath = Resolve-ArtifactPath $MsixPath "tmux-win32.msix"
$MsixSummary = Resolve-ArtifactPath $MsixSummary "tmux-win32.msix.json"
$SigningSummary = Resolve-ArtifactPath $SigningSummary "signing-audit.json"
$CompletionAudit = Resolve-ArtifactPath $CompletionAudit `
    "completion-audit.json"
$IpcBoundarySummary = Resolve-ArtifactPath $IpcBoundarySummary `
    "ipc-boundary-audit.json"
$LinuxParitySummary = Resolve-ArtifactPath $LinuxParitySummary `
    "linux-parity-matrix.json"
$LinuxBehaviorSummary = Resolve-ArtifactPath $LinuxBehaviorSummary `
    "linux-behavior-parity.json"
$HostedCiSummary = Resolve-ArtifactPath $HostedCiSummary `
    "hosted-ci-audit.json"
$SourceStateSummary = Resolve-ArtifactPath $SourceStateSummary `
    "source-state-audit.json"

$requireMsixEffective = $RequireMsix -or $RequireProductionReady
$requireSignedMsixEffective = $RequireSignedMsix -or $RequireProductionReady
$requireSigningAuditEffective = $RequireSigningAudit -or
    $RequireProductionReady
$requireCompletionAuditEffective = $RequireCompletionAudit -or
    $RequireProductionReady
$requireCompletionCompleteEffective = $RequireCompletionComplete -or
    $RequireProductionReady
$requireIpcBoundaryAuditEffective = $RequireIpcBoundaryAudit -or
    $RequireProductionReady
$requireLinuxParityEffective = $RequireLinuxParity -or
    $RequireProductionReady
$requireLinuxBehaviorParityEffective = $RequireLinuxBehaviorParity -or
    $RequireProductionReady
$requireHostedCiAuditEffective = $RequireHostedCiAudit -or
    $RequireProductionReady
$requireHostedCiGreenEffective = $RequireHostedCiGreen -or
    $RequireProductionReady
$requireSourceStateAuditEffective = $RequireSourceStateAudit -or
    $RequireProductionReady

$zipSidecar = $ZipPath + ".sha256"
$expectedZipHash = Read-Sha256Sidecar $zipSidecar
$actualZipHash = Get-Sha256 $ZipPath
Assert-Equal "zip sha256 sidecar" $expectedZipHash $actualZipHash

$manifestPath = Join-Path $Package "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
	throw "manifest not found: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($file in $manifest.Files) {
	if (-not (Test-Path -LiteralPath $file.Path)) {
		throw "manifest file missing: $($file.Path)"
	}
	$hash = Get-Sha256 $file.Path
	Assert-Equal "manifest sha256 for $($file.Name)" $file.SHA256 $hash
}

$release = Get-Content -LiteralPath $ReleaseSummary -Raw | ConvertFrom-Json
Assert-Equal "release summary zip sha256" $actualZipHash $release.ZipSha256

function Assert-ReleaseMinimum([object]$Summary, [string]$Property,
    [int]$Minimum) {
	$value = 0
	if ($Summary.PSObject.Properties.Name -contains $Property) {
		$value = [int]$Summary.$Property
	}
	if ($value -lt $Minimum) {
		throw ("release summary {0} below release minimum: expected >= {1} got {2}" -f `
		    $Property, $Minimum, $value)
	}
}

function Assert-ReleaseStepPassed([object]$Summary, [string]$Name) {
	if (-not ($Summary.PSObject.Properties.Name -contains "Steps")) {
		throw "release summary missing Steps"
	}
	$stepMatches = @($Summary.Steps | Where-Object { $_.Name -eq $Name })
	if ($stepMatches.Count -eq 0) {
		throw "release summary missing required step: $Name"
	}
	$status = [string]$stepMatches[0].Status
	if ($status -ne "passed") {
		throw ("release summary required step did not pass: {0} status={1}" -f `
		    $Name, $status)
	}
}

if ($requireCompletionAuditEffective) {
	foreach ($stepName in @(
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
	    "console-soak",
	    "clipboard-stress")) {
		Assert-ReleaseStepPassed $release $stepName
	}
	Assert-ReleaseMinimum $release "RespawnIterations" 20
	Assert-ReleaseMinimum $release "IpcAclIterations" 3
	Assert-ReleaseMinimum $release "JobStressIterations" 10
	Assert-ReleaseMinimum $release "ClientStressIterations" 5
	Assert-ReleaseMinimum $release "SignalMatrixIterations" 3
	Assert-ReleaseMinimum $release "StressIterations" 1
	Assert-ReleaseMinimum $release "SoakSeconds" 10
	Assert-ReleaseMinimum $release "ConsoleSoakSeconds" 10
	Assert-ReleaseMinimum $release "ConsoleReattachCycles" 2
	Assert-ReleaseMinimum $release "ClipboardStressIterations" 3
	if (-not ($release.PSObject.Properties.Name -contains
	    "RunConfigStress") -or -not [bool]$release.RunConfigStress) {
		throw "release summary RunConfigStress is not enabled"
	}
}

$commandSurface = Get-Content -LiteralPath $CommandSurfaceSummary -Raw |
    ConvertFrom-Json
if ($commandSurface.CommandCount -lt 90 -or
    $commandSurface.GlobalOptionCount -lt 60 -or
    $commandSurface.ServerOptionCount -lt 25 -or
    $commandSurface.WindowOptionCount -lt 70 -or
    $commandSurface.KeyBindingCount -lt 250) {
	throw "command-surface summary counts are below release minimums"
}

$msixHash = ""
$msixSigned = $false
$msixSignatureStatus = ""
if (Test-Path -LiteralPath $MsixPath) {
	$msixHash = Get-Sha256 $MsixPath
	if (-not (Test-Path -LiteralPath $MsixSummary)) {
		throw "MSIX summary not found: $MsixSummary"
	}
	$msix = Get-Content -LiteralPath $MsixSummary -Raw | ConvertFrom-Json
	Assert-Equal "MSIX summary sha256" $msixHash $msix.SHA256
	if ($release.BuildMsix) {
		Assert-Equal "release summary MSIX sha256" $msixHash `
		    $release.MsixSha256
	}
	$signature = Get-AuthenticodeSignature -LiteralPath $MsixPath
	$msixSignatureStatus = [string]$signature.Status
	$msixSigned = $signature.SignerCertificate -ne $null
	if ($requireSignedMsixEffective -and $signature.Status -ne "Valid") {
		throw "MSIX signature is not valid: $msixSignatureStatus"
	}
} elseif ($requireMsixEffective -or $requireSignedMsixEffective -or
    $release.BuildMsix) {
	throw "MSIX artifact not found: $MsixPath"
}

$signingAuditStatus = ""
if (Test-Path -LiteralPath $SigningSummary) {
	$signingAudit = Get-Content -LiteralPath $SigningSummary -Raw |
	    ConvertFrom-Json
	if (-not ($signingAudit.PSObject.Properties.Name -contains "Status")) {
		throw "signing audit missing Status: $SigningSummary"
	}
	$signingAuditStatus = $signingAudit.Status
	if ($signingAudit.PSObject.Properties.Name -contains
	    "SummaryHashMatches" -and
	    $signingAudit.SummaryHashMatches -eq $false) {
		throw "signing audit summary hash does not match MSIX: $SigningSummary"
	}
	if ($signingAudit.PSObject.Properties.Name -contains
	    "SummaryPublisherMatchesManifest" -and
	    $signingAudit.SummaryPublisherMatchesManifest -eq $false) {
		throw "signing audit publisher metadata mismatch: $SigningSummary"
	}
	if ($signingAudit.PSObject.Properties.Name -contains
	    "SignerSubjectMatchesPublisher" -and
	    $signingAudit.SignerSubjectMatchesPublisher -eq $false) {
		throw "signing audit signer subject mismatch: $SigningSummary"
	}
	if ($signingAudit.PSObject.Properties.Name -contains
	    "MetadataMismatches" -and
	    @($signingAudit.MetadataMismatches).Count -gt 0) {
		throw ("signing audit metadata mismatch: {0}" -f `
		    (@($signingAudit.MetadataMismatches) -join ";"))
	}
	if ($requireSignedMsixEffective -and
	    $signingAudit.Status -ne "trusted") {
		throw "signing audit does not report trusted: $SigningSummary"
	}
} elseif ($requireSigningAuditEffective) {
	throw "signing audit not found: $SigningSummary"
}

$completionStatus = ""
if (Test-Path -LiteralPath $CompletionAudit) {
	$completion = Get-Content -LiteralPath $CompletionAudit -Raw |
	    ConvertFrom-Json
	if (-not ($completion.PSObject.Properties.Name -contains "Status")) {
		throw "completion audit missing Status: $CompletionAudit"
	}
	$completionStatus = $completion.Status
	if ($requireCompletionCompleteEffective -and
	    $completionStatus -ne "complete") {
		$missing = 0
		if ($completion.PSObject.Properties.Name -contains "Missing") {
			$missing = @($completion.Missing).Count
		}
		throw ("completion audit is not complete: status={0};missing={1};source={2}" -f `
		    $completionStatus, $missing, $CompletionAudit)
	}
} elseif ($requireCompletionAuditEffective) {
	throw "completion audit not found: $CompletionAudit"
} elseif ($requireCompletionCompleteEffective) {
	throw "completion audit not found: $CompletionAudit"
}

$ipcBoundaryStatus = ""
if (Test-Path -LiteralPath $IpcBoundarySummary) {
	$ipcBoundary = Get-Content -LiteralPath $IpcBoundarySummary -Raw |
	    ConvertFrom-Json
	$ipcFailures = @($ipcBoundary.Checks | Where-Object {
	    $_.Status -eq "failed"
	})
	if ($ipcBoundary.Status -eq "failed" -or $ipcFailures.Count -gt 0) {
		throw "IPC boundary audit has failed checks: $IpcBoundarySummary"
	}
	$ipcBoundaryStatus = $ipcBoundary.Status
} elseif ($requireIpcBoundaryAuditEffective) {
	throw "IPC boundary audit not found: $IpcBoundarySummary"
}

$linuxParityStatus = ""
if (Test-Path -LiteralPath $LinuxParitySummary) {
	$linuxParity = Get-Content -LiteralPath $LinuxParitySummary -Raw |
	    ConvertFrom-Json
	if ($linuxParity.Status -ne "passed" -or
	    [int]$linuxParity.MissingLinuxSurfaceItemsOnWindows -ne 0) {
		throw "Linux surface parity matrix failed: $LinuxParitySummary"
	}
	$linuxParityStatus = $linuxParity.Status
} elseif ($requireLinuxParityEffective) {
	throw "Linux surface parity matrix not found: $LinuxParitySummary"
}

$linuxBehaviorStatus = ""
$linuxBehaviorCategories = ""
if (Test-Path -LiteralPath $LinuxBehaviorSummary) {
	$linuxBehavior = Get-Content -LiteralPath $LinuxBehaviorSummary -Raw |
	    ConvertFrom-Json
	if ($linuxBehavior.Status -ne "passed" -or
	    [int]$linuxBehavior.Failed -ne 0) {
		throw "Linux behavior parity matrix failed: $LinuxBehaviorSummary"
	}
	if (-not ($linuxBehavior.PSObject.Properties.Name -contains
	    "CategoryCoverage")) {
		throw "Linux behavior parity matrix missing CategoryCoverage: $LinuxBehaviorSummary"
	}
	$requiredCategories = @($linuxBehavior.RequiredCategories)
	$categoryCoverage = @($linuxBehavior.CategoryCoverage)
	$missingCategories = [System.Collections.Generic.List[string]]::new()
	foreach ($category in $requiredCategories) {
		$entry = @($categoryCoverage | Where-Object {
		    $_.Category -eq $category -and $_.Covered
		})
		if ($entry.Count -eq 0) {
			$missingCategories.Add([string]$category)
		}
	}
	if ($missingCategories.Count -gt 0) {
		throw ("Linux behavior parity category gaps: {0}" -f `
		    ($missingCategories.ToArray() -join ","))
	}
	$linuxBehaviorStatus = $linuxBehavior.Status
	$linuxBehaviorCategories = $requiredCategories -join ","
} elseif ($requireLinuxBehaviorParityEffective) {
	throw "Linux behavior parity matrix not found: $LinuxBehaviorSummary"
}

$hostedCiStatus = ""
$hostedCiHeadSha = ""
if (Test-Path -LiteralPath $HostedCiSummary) {
	$hostedCi = Get-Content -LiteralPath $HostedCiSummary -Raw |
	    ConvertFrom-Json
	if (-not ($hostedCi.PSObject.Properties.Name -contains "Status")) {
		throw "hosted CI audit missing Status: $HostedCiSummary"
	}
	if ($hostedCi.PSObject.Properties.Name -contains "HeadSha") {
		$hostedCiHeadSha = [string]$hostedCi.HeadSha
	}
	if (($requireHostedCiAuditEffective -or
	    $requireHostedCiGreenEffective) -and
	    [string]::IsNullOrWhiteSpace($hostedCiHeadSha)) {
		throw "hosted CI audit missing target HeadSha: $HostedCiSummary"
	}
	if ($hostedCi.Status -eq "passed") {
		if ($hostedCi.GreenRun -eq $null) {
			throw "hosted CI audit passed without GreenRun: $HostedCiSummary"
		}
		$greenHeadSha = ""
		if ($hostedCi.GreenRun.PSObject.Properties.Name -contains
		    "HeadSha") {
			$greenHeadSha = [string]$hostedCi.GreenRun.HeadSha
		}
		if (-not [string]::IsNullOrWhiteSpace($hostedCiHeadSha) -and
		    $greenHeadSha -ne $hostedCiHeadSha) {
			throw ("hosted CI audit green run head SHA mismatch: expected {0} got {1}" -f `
			    $hostedCiHeadSha, $greenHeadSha)
		}
	}
	if ($requireHostedCiGreenEffective -and
	    $hostedCi.Status -ne "passed") {
		throw ("hosted CI audit is not green: status={0};detail={1};source={2}" -f `
		    $hostedCi.Status, $hostedCi.Detail, $HostedCiSummary)
	}
	$hostedCiStatus = $hostedCi.Status
} elseif ($requireHostedCiAuditEffective -or
    $requireHostedCiGreenEffective) {
	throw "hosted CI audit not found: $HostedCiSummary"
}

$sourceStateStatus = ""
$sourceStateHeadSha = ""
if (Test-Path -LiteralPath $SourceStateSummary) {
	$sourceState = Get-Content -LiteralPath $SourceStateSummary -Raw |
	    ConvertFrom-Json
	foreach ($property in @("HeadSha", "IsDirty", "TrackedChangedCount",
	    "UntrackedCount", "TrackedDiffSha256", "SourceStateFingerprint")) {
		if (-not ($sourceState.PSObject.Properties.Name -contains
		    $property)) {
			throw "source state audit missing $property`: $SourceStateSummary"
		}
	}
	$sourceStateHeadSha = [string]$sourceState.HeadSha
	$sourceStateStatus = $(if ([bool]$sourceState.IsDirty) {
	    "dirty"
	} else { "clean" })
	if ($requireSourceStateAuditEffective -and [bool]$sourceState.IsDirty) {
		throw ("source state is dirty: tracked={0};untracked={1};source={2}" -f `
		    [int]$sourceState.TrackedChangedCount,
		    [int]$sourceState.UntrackedCount,
		    $SourceStateSummary)
	}
} elseif ($requireSourceStateAuditEffective) {
	throw "source state audit not found: $SourceStateSummary"
}

if (-not [string]::IsNullOrWhiteSpace($hostedCiHeadSha) -and
    -not [string]::IsNullOrWhiteSpace($sourceStateHeadSha) -and
    $hostedCiHeadSha -ne $sourceStateHeadSha) {
	throw ("hosted CI and source-state head SHA mismatch: hosted={0};source={1}" -f `
	    $hostedCiHeadSha, $sourceStateHeadSha)
}

Write-Host "Windows release artifacts verified."
Write-Host "zip=$ZipPath"
Write-Host "zip_sha256=$actualZipHash"
if (-not [string]::IsNullOrWhiteSpace($msixHash)) {
	Write-Host "msix=$MsixPath"
	Write-Host "msix_sha256=$msixHash"
	Write-Host "msix_signed=$msixSigned"
	Write-Host "msix_signature_status=$msixSignatureStatus"
}
if (-not [string]::IsNullOrWhiteSpace($completionStatus)) {
	Write-Host "completion_audit_status=$completionStatus"
}
if (-not [string]::IsNullOrWhiteSpace($signingAuditStatus)) {
	Write-Host "signing_audit_status=$signingAuditStatus"
}
if (-not [string]::IsNullOrWhiteSpace($ipcBoundaryStatus)) {
	Write-Host "ipc_boundary_status=$ipcBoundaryStatus"
}
if (-not [string]::IsNullOrWhiteSpace($linuxParityStatus)) {
	Write-Host "linux_surface_parity_status=$linuxParityStatus"
}
if (-not [string]::IsNullOrWhiteSpace($linuxBehaviorStatus)) {
	Write-Host "linux_behavior_parity_status=$linuxBehaviorStatus"
	Write-Host "linux_behavior_categories=$linuxBehaviorCategories"
}
if (-not [string]::IsNullOrWhiteSpace($hostedCiStatus)) {
	Write-Host "hosted_ci_status=$hostedCiStatus"
	if (-not [string]::IsNullOrWhiteSpace($hostedCiHeadSha)) {
		Write-Host "hosted_ci_head_sha=$hostedCiHeadSha"
	}
}
if (-not [string]::IsNullOrWhiteSpace($sourceStateStatus)) {
	Write-Host "source_state=$sourceStateStatus"
	Write-Host "source_state_head_sha=$sourceStateHeadSha"
}
