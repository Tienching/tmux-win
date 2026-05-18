param(
	[string]$CC = $(if ($env:CC) { $env:CC } else { "gcc" }),
	[string]$CXX = $(if ($env:CXX) { $env:CXX } else { "g++" }),
	[string]$Yacc = $(if ($env:YACC) { $env:YACC } else { "" }),
	[string]$Tmux = "",
	[string]$Output = "",
	[string]$Package = "",
	[string]$ZipPath = "",
	[string]$SummaryPath = "",
	[string]$CommandSurfaceSummaryPath = "",
	[string]$VisualTerminalSummaryPath = "",
	[string]$MsixPath = "",
	[string]$MsixSummaryPath = "",
	[string]$MsixPublisher = "CN=tmux",
	[string]$MsixCertificatePath = "",
	[string]$MsixCertificatePassword = "",
	[string]$MsixCertificateThumbprint = "",
	[int]$SmokeTimeoutSeconds = 60,
	[int]$RespawnIterations = 0,
	[int]$IpcAclIterations = 0,
	[int]$JobStressIterations = 0,
	[int]$JobStressBackgroundJobs = 8,
	[int]$ClientStressIterations = 0,
	[int]$ClientStressCommandClients = 8,
	[int]$SignalMatrixIterations = 0,
	[int]$StressIterations = 0,
	[int]$SoakSeconds = 0,
	[int]$ConsoleSoakSeconds = 0,
	[int]$ConsoleReattachCycles = 2,
	[int]$ClipboardStressIterations = 0,
	[int]$ClipboardStressHoldMilliseconds = 500,
	[switch]$BuildMsix,
	[switch]$SignMsix,
	[switch]$SkipCommandSurfaceAudit,
	[switch]$RunVisualTerminalVerify,
	[switch]$RunConfigStress,
	[switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = [System.IO.Path]::GetFullPath($Tmux)

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = $Tmux
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

if ([string]::IsNullOrWhiteSpace($Package)) {
	$Package = Join-Path $Root "dist\tmux-win32-portable"
} elseif (-not [System.IO.Path]::IsPathRooted($Package)) {
	$Package = Join-Path (Get-Location) $Package
}
$Package = [System.IO.Path]::GetFullPath($Package)

if ([string]::IsNullOrWhiteSpace($ZipPath)) {
	$ZipPath = $Package.TrimEnd('\', '/') + ".zip"
} elseif (-not [System.IO.Path]::IsPathRooted($ZipPath)) {
	$ZipPath = Join-Path (Get-Location) $ZipPath
}
$ZipPath = [System.IO.Path]::GetFullPath($ZipPath)
$ZipHashPath = $ZipPath + ".sha256"

if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
	$packageParent = Split-Path -Parent $Package
	if ([string]::IsNullOrWhiteSpace($packageParent)) {
		$packageParent = Get-Location
	}
	$SummaryPath = Join-Path $packageParent "release-check.json"
} elseif (-not [System.IO.Path]::IsPathRooted($SummaryPath)) {
	$SummaryPath = Join-Path (Get-Location) $SummaryPath
}
$SummaryPath = [System.IO.Path]::GetFullPath($SummaryPath)

if ([string]::IsNullOrWhiteSpace($CommandSurfaceSummaryPath)) {
	$packageParent = Split-Path -Parent $Package
	if ([string]::IsNullOrWhiteSpace($packageParent)) {
		$packageParent = Get-Location
	}
	$CommandSurfaceSummaryPath = Join-Path $packageParent `
	    "command-surface.json"
} elseif (-not [System.IO.Path]::IsPathRooted($CommandSurfaceSummaryPath)) {
	$CommandSurfaceSummaryPath = Join-Path (Get-Location) `
	    $CommandSurfaceSummaryPath
}
$CommandSurfaceSummaryPath =
    [System.IO.Path]::GetFullPath($CommandSurfaceSummaryPath)

if ([string]::IsNullOrWhiteSpace($VisualTerminalSummaryPath)) {
	$packageParent = Split-Path -Parent $Package
	if ([string]::IsNullOrWhiteSpace($packageParent)) {
		$packageParent = Get-Location
	}
	$VisualTerminalSummaryPath = Join-Path $packageParent `
	    "visual-terminal-verify.txt"
} elseif (-not [System.IO.Path]::IsPathRooted($VisualTerminalSummaryPath)) {
	$VisualTerminalSummaryPath = Join-Path (Get-Location) `
	    $VisualTerminalSummaryPath
}
$VisualTerminalSummaryPath =
    [System.IO.Path]::GetFullPath($VisualTerminalSummaryPath)

if ([string]::IsNullOrWhiteSpace($MsixPath)) {
	$MsixPath = Join-Path (Split-Path -Parent $Package) "tmux-win32.msix"
} elseif (-not [System.IO.Path]::IsPathRooted($MsixPath)) {
	$MsixPath = Join-Path (Get-Location) $MsixPath
}
$MsixPath = [System.IO.Path]::GetFullPath($MsixPath)

if ([string]::IsNullOrWhiteSpace($MsixSummaryPath)) {
	$MsixSummaryPath = $MsixPath + ".json"
} elseif (-not [System.IO.Path]::IsPathRooted($MsixSummaryPath)) {
	$MsixSummaryPath = Join-Path (Get-Location) $MsixSummaryPath
}
$MsixSummaryPath = [System.IO.Path]::GetFullPath($MsixSummaryPath)

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

$build = Join-Path $PSScriptRoot "build-mingw.ps1"
$packageScript = Join-Path $PSScriptRoot "package-mingw.ps1"
$msixScript = Join-Path $PSScriptRoot "package-msix.ps1"
$installScript = Join-Path $PSScriptRoot "install-portable.ps1"
$commandAudit = Join-Path $PSScriptRoot "audit-command-surface.ps1"
$respawnStress = Join-Path $PSScriptRoot "respawn-stress.ps1"
$ipcAclStress = Join-Path $PSScriptRoot "ipc-acl-stress.ps1"
$jobStress = Join-Path $PSScriptRoot "job-stress.ps1"
$clientStress = Join-Path $PSScriptRoot "client-lifecycle-stress.ps1"
$signalMatrixStress = Join-Path $PSScriptRoot "signal-matrix-stress.ps1"
$configStress = Join-Path $PSScriptRoot "config-parser-stress.ps1"
$visualTerminalVerify = Join-Path $PSScriptRoot "visual-terminal-verify.ps1"
$stress = Join-Path $PSScriptRoot "stress-runtime.ps1"
$soak = Join-Path $PSScriptRoot "soak-runtime.ps1"
$consoleSoak = Join-Path $PSScriptRoot "console-attach-soak.ps1"
$clipboardStress = Join-Path $PSScriptRoot "clipboard-stress.ps1"
$steps = [System.Collections.Generic.List[object]]::new()

function Add-Step([string]$Name, [string]$Status, [string]$Detail = "") {
	$steps.Add([pscustomobject]@{
	    Name = $Name
	    Status = $Status
	    Detail = $Detail
	})
}

if (-not $SkipBuild) {
	$buildArgs = @{
		CC = $CC
		CXX = $CXX
		Output = $Output
	}
	if (-not [string]::IsNullOrWhiteSpace($Yacc)) {
		$buildArgs.Yacc = $Yacc
	}
	& $build @buildArgs
	Add-Step "build" "passed" $Output
} else {
	Add-Step "build" "skipped" $Output
}
if (-not (Test-Path -LiteralPath $Output)) {
	throw "tmux.exe not found after build: $Output"
}
if ($SignMsix -and -not $BuildMsix) {
	throw "-SignMsix requires -BuildMsix"
}
if ($SignMsix -and [string]::IsNullOrWhiteSpace($MsixCertificatePath) -and
    [string]::IsNullOrWhiteSpace($MsixCertificateThumbprint)) {
	throw ("-SignMsix requires -MsixCertificatePath or " +
	    "-MsixCertificateThumbprint")
}

& $packageScript -Tmux $Output -Output $Package -ZipPath $ZipPath `
    -SmokeTimeoutSeconds $SmokeTimeoutSeconds -Clean -Zip -RunSmoke
Add-Step "package-smoke" "passed" $Package

if (-not (Test-Path -LiteralPath $ZipPath)) {
	throw "zip not created: $ZipPath"
}
$expectedZipHash = Read-Sha256Sidecar $ZipHashPath
$actualZipHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).
    Hash.ToLowerInvariant()
if ($actualZipHash -ne $expectedZipHash) {
	throw "zip sha256 mismatch: expected $expectedZipHash got $actualZipHash"
}
Add-Step "zip-sha256" "passed" $actualZipHash

$manifestPath = Join-Path $Package "manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath)) {
	throw "manifest not created: $manifestPath"
}
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
foreach ($file in $manifest.Files) {
	if (-not (Test-Path -LiteralPath $file.Path)) {
		throw "manifest file missing: $($file.Path)"
	}
	$hash = (Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).
	    Hash.ToLowerInvariant()
	if ($hash -ne $file.SHA256) {
		throw "manifest sha256 mismatch: $($file.Name)"
	}
}
Add-Step "manifest-hashes" "passed" ("files={0}" -f @($manifest.Files).Count)

if (-not $SkipCommandSurfaceAudit) {
	& $commandAudit -Tmux (Join-Path $Package "tmux.exe") `
	    -SummaryPath $CommandSurfaceSummaryPath `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "command-surface" "passed" $CommandSurfaceSummaryPath
} else {
	Add-Step "command-surface" "skipped" $CommandSurfaceSummaryPath
}

$msixHash = ""
if ($BuildMsix) {
	$msixArgs = @{
		Package = $Package
		Output = $MsixPath
		SummaryPath = $MsixSummaryPath
		Publisher = $MsixPublisher
	}
	if ($SignMsix) {
		$msixArgs.Sign = $true
		if (-not [string]::IsNullOrWhiteSpace($MsixCertificatePath)) {
			$msixArgs.CertificatePath = $MsixCertificatePath
		}
		if (-not [string]::IsNullOrWhiteSpace($MsixCertificatePassword)) {
			$msixArgs.CertificatePassword = $MsixCertificatePassword
		}
		if (-not [string]::IsNullOrWhiteSpace(
		    $MsixCertificateThumbprint)) {
			$msixArgs.CertificateThumbprint =
			    $MsixCertificateThumbprint
		}
	}
	& $msixScript @msixArgs
	$msixHash = (Get-FileHash -LiteralPath $MsixPath -Algorithm SHA256).
	    Hash.ToLowerInvariant()
	Add-Step "msix-package" "passed" $msixHash
} else {
	Add-Step "msix-package" "skipped" $MsixPath
}

if ($RespawnIterations -gt 0) {
	& $respawnStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $RespawnIterations -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "respawn-stress" "passed" `
	    ("iterations={0}" -f $RespawnIterations)
} else {
	Add-Step "respawn-stress" "skipped" "iterations=0"
}

if ($IpcAclIterations -gt 0) {
	& $ipcAclStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $IpcAclIterations `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "ipc-acl-stress" "passed" `
	    ("iterations={0}" -f $IpcAclIterations)
} else {
	Add-Step "ipc-acl-stress" "skipped" "iterations=0"
}

if ($JobStressIterations -gt 0) {
	& $jobStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $JobStressIterations `
	    -BackgroundJobs $JobStressBackgroundJobs `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "job-stress" "passed" `
	    ("iterations={0};background_jobs={1}" -f $JobStressIterations,
	    $JobStressBackgroundJobs)
} else {
	Add-Step "job-stress" "skipped" "iterations=0"
}

if ($ClientStressIterations -gt 0) {
	& $clientStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $ClientStressIterations `
	    -CommandClients $ClientStressCommandClients `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "client-lifecycle-stress" "passed" `
	    ("iterations={0};command_clients={1}" -f `
	    $ClientStressIterations, $ClientStressCommandClients)
} else {
	Add-Step "client-lifecycle-stress" "skipped" "iterations=0"
}

if ($SignalMatrixIterations -gt 0) {
	& $signalMatrixStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $SignalMatrixIterations `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "signal-matrix-stress" "passed" `
	    ("iterations={0}" -f $SignalMatrixIterations)
} else {
	Add-Step "signal-matrix-stress" "skipped" "iterations=0"
}

if ($RunConfigStress) {
	& $configStress -Tmux (Join-Path $Package "tmux.exe") `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "config-parser-stress" "passed" "enabled"
} else {
	Add-Step "config-parser-stress" "skipped" "disabled"
}

$installCheck = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("tmux-install-check-" + [Guid]::NewGuid().ToString("N"))
try {
	& $installScript -ZipPath $ZipPath -InstallDir $installCheck `
	    -Force -Verify
	$installedTmux = Join-Path $installCheck "tmux.exe"
	$installedVersion = (& $installedTmux -V 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "installed tmux.exe failed to start"
	}
	if (($installedVersion -join "`n").Trim() -ne $manifest.Version) {
		throw "installed tmux.exe version mismatch"
	}
	& $installScript -InstallDir $installCheck -Uninstall
	Add-Step "zip-install-uninstall" "passed" $manifest.Version
} finally {
	if (Test-Path -LiteralPath $installCheck) {
		$tempRoot = [System.IO.Path]::GetTempPath()
		$installFull = [System.IO.Path]::GetFullPath($installCheck)
		if ($installFull.StartsWith($tempRoot,
		    [System.StringComparison]::OrdinalIgnoreCase)) {
			Remove-Item -LiteralPath $installFull -Recurse -Force
		}
	}
}

if ($StressIterations -gt 0) {
	& $stress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $StressIterations -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "stress" "passed" ("iterations={0}" -f $StressIterations)
} else {
	Add-Step "stress" "skipped" "iterations=0"
}
if ($SoakSeconds -gt 0) {
	& $soak -Tmux (Join-Path $Package "tmux.exe") `
	    -DurationSeconds $SoakSeconds
	Add-Step "soak" "passed" ("seconds={0}" -f $SoakSeconds)
} else {
	Add-Step "soak" "skipped" "seconds=0"
}
if ($ConsoleSoakSeconds -gt 0) {
	& $consoleSoak -Tmux (Join-Path $Package "tmux.exe") `
	    -DurationSeconds $ConsoleSoakSeconds `
	    -ReattachCycles $ConsoleReattachCycles `
	    -TimeoutSeconds $SmokeTimeoutSeconds
	Add-Step "console-soak" "passed" `
	    ("seconds={0};reattach_cycles={1}" -f $ConsoleSoakSeconds,
	    $ConsoleReattachCycles)
} else {
	Add-Step "console-soak" "skipped" `
	    ("seconds=0;reattach_cycles={0}" -f $ConsoleReattachCycles)
}

if ($ClipboardStressIterations -gt 0) {
	& $clipboardStress -Tmux (Join-Path $Package "tmux.exe") `
	    -Iterations $ClipboardStressIterations `
	    -HoldMilliseconds $ClipboardStressHoldMilliseconds `
	    -TimeoutSeconds $SmokeTimeoutSeconds `
	    -RequireAvailable
	Add-Step "clipboard-stress" "passed" `
	    ("iterations={0};hold_ms={1}" -f $ClipboardStressIterations,
	    $ClipboardStressHoldMilliseconds)
} else {
	Add-Step "clipboard-stress" "skipped" "iterations=0"
}

if ($RunVisualTerminalVerify) {
	& $visualTerminalVerify -Tmux (Join-Path $Package "tmux.exe") `
	    -ResultPath $VisualTerminalSummaryPath
	Add-Step "visual-terminal-verify" "passed" `
	    $VisualTerminalSummaryPath
} else {
	Add-Step "visual-terminal-verify" "skipped" `
	    $VisualTerminalSummaryPath
}

$summaryDirectory = Split-Path -Parent $SummaryPath
if (-not [string]::IsNullOrWhiteSpace($summaryDirectory)) {
	New-Item -ItemType Directory -Force -Path $summaryDirectory | Out-Null
}
$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Root = $Root
	Tmux = $Output
	Package = $Package
	Zip = $ZipPath
	ZipSha256 = $actualZipHash
	Manifest = $manifestPath
	Version = $manifest.Version
	SmokeTimeoutSeconds = $SmokeTimeoutSeconds
	RespawnIterations = $RespawnIterations
	IpcAclIterations = $IpcAclIterations
	JobStressIterations = $JobStressIterations
	JobStressBackgroundJobs = $JobStressBackgroundJobs
	ClientStressIterations = $ClientStressIterations
	ClientStressCommandClients = $ClientStressCommandClients
	SignalMatrixIterations = $SignalMatrixIterations
	RunConfigStress = [bool]$RunConfigStress
	StressIterations = $StressIterations
	SoakSeconds = $SoakSeconds
	ConsoleSoakSeconds = $ConsoleSoakSeconds
	ConsoleReattachCycles = $ConsoleReattachCycles
	ClipboardStressIterations = $ClipboardStressIterations
	ClipboardStressHoldMilliseconds = $ClipboardStressHoldMilliseconds
	RunVisualTerminalVerify = [bool]$RunVisualTerminalVerify
	VisualTerminalSummary = $VisualTerminalSummaryPath
	CommandSurfaceSummary = $CommandSurfaceSummaryPath
	SkipCommandSurfaceAudit = [bool]$SkipCommandSurfaceAudit
	BuildMsix = [bool]$BuildMsix
	SignMsix = [bool]$SignMsix
	MsixPublisher = $(if ($BuildMsix) { $MsixPublisher } else { "" })
	Msix = $(if ($BuildMsix) { $MsixPath } else { "" })
	MsixSha256 = $msixHash
	MsixSummary = $(if ($BuildMsix) { $MsixSummaryPath } else { "" })
	ManifestFiles = @($manifest.Files).Count
	Dependencies = @($manifest.Dependencies).Count
	Steps = @($steps.ToArray())
}
$summary | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $SummaryPath -Encoding ascii

Write-Host "Windows release check passed."
Write-Host "tmux=$Output"
Write-Host "package=$Package"
Write-Host "zip=$ZipPath"
Write-Host "zip_sha256=$actualZipHash"
if ($BuildMsix) {
	Write-Host "msix=$MsixPath"
	Write-Host "msix_sha256=$msixHash"
}
Write-Host "summary=$SummaryPath"
