param(
	[string]$Package = "",
	[string]$ZipPath = "",
	[string]$InstallDir = "",
	[switch]$AddToUserPath,
	[switch]$Uninstall,
	[switch]$Force,
	[switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$MarkerName = "install.json"
$InstallerId = "tmux-win32-portable"

function Resolve-FullPath([string]$Path) {
	if ([string]::IsNullOrWhiteSpace($Path)) {
		return ""
	}
	if ([System.IO.Path]::IsPathRooted($Path)) {
		return [System.IO.Path]::GetFullPath($Path)
	}
	return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Get-DefaultInstallDir {
	if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
		throw "LOCALAPPDATA is not set"
	}
	return Join-Path $env:LOCALAPPDATA "tmux"
}

function Read-PackageManifest([string]$SourceDir) {
	$manifestPath = Join-Path $SourceDir "manifest.json"
	if (-not (Test-Path -LiteralPath $manifestPath)) {
		throw "manifest not found: $manifestPath"
	}
	$manifest = Get-Content -LiteralPath $manifestPath -Raw |
	    ConvertFrom-Json
	foreach ($file in $manifest.Files) {
		$sourceFile = Join-Path $SourceDir $file.Name
		if (-not (Test-Path -LiteralPath $sourceFile)) {
			throw "package file missing: $sourceFile"
		}
		$hash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).
		    Hash.ToLowerInvariant()
		if ($hash -ne $file.SHA256) {
			throw "package sha256 mismatch: $($file.Name)"
		}
	}
	return $manifest
}

function Test-InstallMarker([string]$Path) {
	$marker = Join-Path $Path $MarkerName
	if (-not (Test-Path -LiteralPath $marker)) {
		return $false
	}
	try {
		$data = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
		return ($data.Installer -eq $InstallerId)
	} catch {
		return $false
	}
}

function Update-UserPath([string]$Path, [bool]$Add) {
	$current = [Environment]::GetEnvironmentVariable("Path", "User")
	if ($null -eq $current) {
		$current = ""
	}
	$parts = @($current -split ";" | Where-Object {
	    -not [string]::IsNullOrWhiteSpace($_)
	})
	$exists = @($parts | Where-Object {
	    $_.TrimEnd('\', '/') -ieq $Path.TrimEnd('\', '/')
	}).Count -ne 0
	if ($Add) {
		if (-not $exists) {
			$parts += $Path
		}
	} else {
		$parts = @($parts | Where-Object {
		    $_.TrimEnd('\', '/') -ine $Path.TrimEnd('\', '/')
		})
	}
	[Environment]::SetEnvironmentVariable("Path", ($parts -join ";"),
	    "User")
}

function Verify-Install([string]$Path) {
	$markerPath = Join-Path $Path $MarkerName
	if (-not (Test-Path -LiteralPath $markerPath)) {
		throw "install marker not found: $markerPath"
	}
	$marker = Get-Content -LiteralPath $markerPath -Raw | ConvertFrom-Json
	if ($marker.Installer -ne $InstallerId) {
		throw "unexpected install marker: $markerPath"
	}
	foreach ($file in $marker.Files) {
		$installedFile = Join-Path $Path $file.Name
		if (-not (Test-Path -LiteralPath $installedFile)) {
			throw "installed file missing: $installedFile"
		}
		$hash = (Get-FileHash -LiteralPath $installedFile `
		    -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($hash -ne $file.SHA256) {
			throw "installed sha256 mismatch: $($file.Name)"
		}
	}
	$tmux = Join-Path $Path "tmux.exe"
	$version = (& $tmux -V 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "installed tmux.exe failed to start"
	}
	return ($version -join "`n").Trim()
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
	$InstallDir = Get-DefaultInstallDir
}
$InstallDir = Resolve-FullPath $InstallDir

if ($Uninstall) {
	if (Test-Path -LiteralPath $InstallDir) {
		if (-not (Test-InstallMarker $InstallDir)) {
			throw "refusing to uninstall unmarked directory: $InstallDir"
		}
		if ($AddToUserPath) {
			Update-UserPath $InstallDir $false
		}
		Remove-Item -LiteralPath $InstallDir -Recurse -Force
	}
	Write-Host "uninstalled=$InstallDir"
	return
}

if ([string]::IsNullOrWhiteSpace($Package) -and
    [string]::IsNullOrWhiteSpace($ZipPath)) {
	$Package = Join-Path $Root "dist\tmux-win32-portable"
}
if (-not [string]::IsNullOrWhiteSpace($Package) -and
    -not [string]::IsNullOrWhiteSpace($ZipPath)) {
	throw "pass either -Package or -ZipPath, not both"
}

$tempExtract = ""
try {
	if (-not [string]::IsNullOrWhiteSpace($ZipPath)) {
		$ZipPath = Resolve-FullPath $ZipPath
		if (-not (Test-Path -LiteralPath $ZipPath)) {
			throw "zip not found: $ZipPath"
		}
		$tempExtract = Join-Path ([System.IO.Path]::GetTempPath()) `
		    ("tmux-install-" + [Guid]::NewGuid().ToString("N"))
		New-Item -ItemType Directory -Force -Path $tempExtract | Out-Null
		Expand-Archive -LiteralPath $ZipPath -DestinationPath `
		    $tempExtract -Force
		$Package = $tempExtract
	} else {
		$Package = Resolve-FullPath $Package
	}
	if (-not (Test-Path -LiteralPath $Package)) {
		throw "package directory not found: $Package"
	}

	$manifest = Read-PackageManifest $Package
	if (Test-Path -LiteralPath $InstallDir) {
		if (-not $Force) {
			throw "install directory exists; pass -Force: $InstallDir"
		}
		if (-not (Test-InstallMarker $InstallDir)) {
			$children = @(Get-ChildItem -LiteralPath $InstallDir `
			    -Force -ErrorAction SilentlyContinue)
			if ($children.Count -ne 0) {
				throw "refusing to replace unmarked non-empty " +
				    "directory: $InstallDir"
			}
		}
		Remove-Item -LiteralPath $InstallDir -Recurse -Force
	}
	New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
	Get-ChildItem -LiteralPath $Package -Force | ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination $InstallDir `
		    -Force
	}

	$installManifest = [pscustomobject]@{
		Installer = $InstallerId
		InstalledAt = (Get-Date).ToUniversalTime().ToString("o")
		Source = $Package
		Version = $manifest.Version
		Files = $manifest.Files
	}
	$installManifest | ConvertTo-Json -Depth 4 |
	    Set-Content -LiteralPath (Join-Path $InstallDir $MarkerName) `
	    -Encoding ascii

	$version = Verify-Install $InstallDir
	if ($Verify -and $version -ne $manifest.Version) {
		throw "installed version mismatch: $version"
	}
	if ($AddToUserPath) {
		Update-UserPath $InstallDir $true
	}

	Write-Host "installed=$InstallDir"
	Write-Host "tmux=$(Join-Path $InstallDir 'tmux.exe')"
	Write-Host "version=$version"
	if ($AddToUserPath) {
		Write-Host "user_path=updated"
	}
} finally {
	if (-not [string]::IsNullOrWhiteSpace($tempExtract) -and
	    (Test-Path -LiteralPath $tempExtract)) {
		Remove-Item -LiteralPath $tempExtract -Recurse -Force
	}
}
