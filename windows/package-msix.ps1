param(
	[string]$Package = "",
	[string]$Output = "",
	[string]$MakeAppx = "",
	[string]$SignTool = "",
	[string]$IdentityName = "tmux.windows",
	[string]$Publisher = "CN=tmux",
	[string]$PublisherDisplayName = "tmux",
	[string]$DisplayName = "tmux",
	[string]$Description = "tmux for Windows",
	[string]$Version = "",
	[string]$Architecture = "x64",
	[string]$Alias = "tmux.exe",
	[string]$TimestampUrl = "http://timestamp.digicert.com",
	[string]$CertificatePath = "",
	[string]$CertificatePassword = "",
	[SecureString]$CertificatePasswordSecure = $null,
	[string]$CertificateThumbprint = "",
	[string]$SummaryPath = "",
	[switch]$Sign,
	[switch]$KeepStaging
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Package)) {
	$Package = Join-Path $Root "dist\tmux-win32-portable"
} elseif (-not [System.IO.Path]::IsPathRooted($Package)) {
	$Package = Join-Path (Get-Location) $Package
}
$Package = (Resolve-Path -LiteralPath $Package).Path

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "dist\tmux-win32.msix"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

if ([string]::IsNullOrWhiteSpace($SummaryPath)) {
	$SummaryPath = $Output + ".json"
} elseif (-not [System.IO.Path]::IsPathRooted($SummaryPath)) {
	$SummaryPath = Join-Path (Get-Location) $SummaryPath
}
$SummaryPath = [System.IO.Path]::GetFullPath($SummaryPath)

$packagedTmux = Join-Path $Package "tmux.exe"
if (-not (Test-Path -LiteralPath $packagedTmux)) {
	throw "packaged tmux.exe not found: $packagedTmux"
}

function Resolve-ToolPath([string]$Tool) {
	if ([string]::IsNullOrWhiteSpace($Tool)) {
		return ""
	}
	if ([System.IO.Path]::IsPathRooted($Tool) -and
	    (Test-Path -LiteralPath $Tool)) {
		return (Resolve-Path -LiteralPath $Tool).Path
	}
	$cmd = Get-Command $Tool -ErrorAction SilentlyContinue
	if ($cmd -ne $null -and -not [string]::IsNullOrWhiteSpace($cmd.Source)) {
		return $cmd.Source
	}
	return ""
}

function Find-WindowsKitTool([string]$Name) {
	$kitRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
	if (-not (Test-Path -LiteralPath $kitRoot)) {
		return ""
	}
	$matches = @(Get-ChildItem -LiteralPath $kitRoot -Recurse `
	    -Filter $Name -ErrorAction SilentlyContinue |
	    Where-Object { $_.FullName -like "*\x64\$Name" } |
	    Sort-Object FullName -Descending)
	if ($matches.Count -gt 0) {
		return $matches[0].FullName
	}
	return ""
}

function Resolve-MakeAppx {
	if (-not [string]::IsNullOrWhiteSpace($MakeAppx)) {
		$resolved = Resolve-ToolPath $MakeAppx
		if (-not [string]::IsNullOrWhiteSpace($resolved)) {
			return $resolved
		}
		throw "makeappx not found: $MakeAppx"
	}
	$resolved = Resolve-ToolPath "makeappx"
	if (-not [string]::IsNullOrWhiteSpace($resolved)) {
		return $resolved
	}
	$resolved = Find-WindowsKitTool "makeappx.exe"
	if (-not [string]::IsNullOrWhiteSpace($resolved)) {
		return $resolved
	}
	throw "makeappx.exe not found; install the Windows SDK or pass -MakeAppx"
}

function Resolve-SignTool {
	if (-not [string]::IsNullOrWhiteSpace($SignTool)) {
		$resolved = Resolve-ToolPath $SignTool
		if (-not [string]::IsNullOrWhiteSpace($resolved)) {
			return $resolved
		}
		throw "signtool not found: $SignTool"
	}
	$resolved = Resolve-ToolPath "signtool"
	if (-not [string]::IsNullOrWhiteSpace($resolved)) {
		return $resolved
	}
	$resolved = Find-WindowsKitTool "signtool.exe"
	if (-not [string]::IsNullOrWhiteSpace($resolved)) {
		return $resolved
	}
	throw "signtool.exe not found; install the Windows SDK or pass -SignTool"
}

function ConvertTo-MsixVersion([string]$InputVersion) {
	if ([string]::IsNullOrWhiteSpace($InputVersion)) {
		$tmuxVersion = (& $packagedTmux -V 2>&1)
		if ($LASTEXITCODE -ne 0) {
			throw "packaged tmux.exe failed to report version"
		}
		$InputVersion = ($tmuxVersion -join "`n").Trim()
	}
	if ($InputVersion -notmatch '([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?(?:\.([0-9]+))?') {
		throw "cannot convert version to MSIX identity version: $InputVersion"
	}
	$parts = @(
	    [int]$Matches[1],
	    $(if ($Matches[2]) { [int]$Matches[2] } else { 0 }),
	    $(if ($Matches[3]) { [int]$Matches[3] } else { 0 }),
	    $(if ($Matches[4]) { [int]$Matches[4] } else { 0 }))
	return ($parts -join ".")
}

function Escape-Xml([string]$Value) {
	return [System.Security.SecurityElement]::Escape($Value)
}

function New-PngAsset([string]$Path, [int]$Size) {
	Add-Type -AssemblyName System.Drawing
	$bitmap = [System.Drawing.Bitmap]::new($Size, $Size)
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	try {
		$graphics.Clear([System.Drawing.Color]::FromArgb(0, 31, 35, 40))
		$fontSize = [Math]::Max(10, [Math]::Floor($Size / 4))
		$font = [System.Drawing.Font]::new("Consolas", $fontSize,
		    [System.Drawing.FontStyle]::Bold,
		    [System.Drawing.GraphicsUnit]::Pixel)
		$brush = [System.Drawing.SolidBrush]::new(
		    [System.Drawing.Color]::White)
		try {
			$text = "tm"
			$measure = $graphics.MeasureString($text, $font)
			$x = ($Size - $measure.Width) / 2
			$y = ($Size - $measure.Height) / 2
			$graphics.DrawString($text, $font, $brush, $x, $y)
		} finally {
			$brush.Dispose()
			$font.Dispose()
		}
		$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	} finally {
		$graphics.Dispose()
		$bitmap.Dispose()
	}
}

function Invoke-Native([string]$Tool, [string[]]$Arguments) {
	$output = & $Tool @Arguments 2>&1
	if ($LASTEXITCODE -ne 0) {
		$output | Select-Object -First 80 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "$([System.IO.Path]::GetFileName($Tool)) failed"
	}
	return $output
}

function Get-CertificateEnhancedKeyUsageOids(
    [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate) {
	$oids = [System.Collections.Generic.List[string]]::new()
	foreach ($extension in @($Certificate.Extensions)) {
		if ($extension.Oid.Value -ne "2.5.29.37") {
			continue
		}
		$eku = [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new(
		    $extension, $extension.Critical)
		foreach ($oid in @($eku.EnhancedKeyUsages)) {
			$oids.Add([string]$oid.Value)
		}
	}
	return $oids.ToArray()
}

function Get-SigningCertificate {
	if (-not [string]::IsNullOrWhiteSpace($CertificatePath)) {
		$resolvedCertificate = (Resolve-Path -LiteralPath `
		    $CertificatePath).Path
		return [System.Security.Cryptography.X509Certificates.X509Certificate2]::new(
		    $resolvedCertificate, $CertificatePassword)
	}
	if (-not [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
		$thumbprint = $CertificateThumbprint.Replace(" ", "")
		foreach ($store in @("Cert:\CurrentUser\My",
		    "Cert:\LocalMachine\My")) {
			$certificate = Get-ChildItem -Path $store `
			    -ErrorAction SilentlyContinue |
			    Where-Object { $_.Thumbprint -ieq $thumbprint } |
			    Select-Object -First 1
			if ($certificate -ne $null) {
				return $certificate
			}
		}
	}
	return $null
}

function Test-SigningCertificateReady(
    [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate) {
	if ($Certificate -eq $null) {
		throw ("signing certificate not found: " +
		    "thumbprint='$CertificateThumbprint' path='$CertificatePath'")
	}
	if ([string]::IsNullOrWhiteSpace($Certificate.Subject)) {
		throw "signing certificate has no subject"
	}
	if ($Certificate.Subject -ne $Publisher) {
		throw ("MSIX Publisher must match signing certificate subject: " +
		    "Publisher='$Publisher' Subject='$($Certificate.Subject)'")
	}
	if (-not [bool]$Certificate.HasPrivateKey) {
		throw ("signing certificate has no private key: " +
		    "Subject='$($Certificate.Subject)'")
	}
	$now = [DateTime]::UtcNow
	if ($Certificate.NotBefore.ToUniversalTime() -gt $now -or
	    $Certificate.NotAfter.ToUniversalTime() -lt $now) {
		throw ("signing certificate is outside its validity window: " +
		    "Subject='$($Certificate.Subject)' NotBefore='$($Certificate.NotBefore.ToString("o"))' NotAfter='$($Certificate.NotAfter.ToString("o"))'")
	}
	$codeSigningOid = "1.3.6.1.5.5.7.3.3"
	$ekuOids = @(Get-CertificateEnhancedKeyUsageOids $Certificate)
	if ($ekuOids -notcontains $codeSigningOid) {
		throw ("signing certificate does not include Code Signing EKU " +
		    "$codeSigningOid`: Subject='$($Certificate.Subject)'")
	}
	return [pscustomobject]@{
	    Subject = $Certificate.Subject
	    Thumbprint = $Certificate.Thumbprint
	    HasPrivateKey = [bool]$Certificate.HasPrivateKey
	    NotBefore = $Certificate.NotBefore.ToString("o")
	    NotAfter = $Certificate.NotAfter.ToString("o")
	    EnhancedKeyUsageOids = $ekuOids
	}
}

if ($Sign -and [string]::IsNullOrWhiteSpace($CertificatePath) -and
    [string]::IsNullOrWhiteSpace($CertificateThumbprint)) {
	throw "pass -CertificatePath or -CertificateThumbprint when using -Sign"
}
$signingCertificateInfo = $null
if ($Sign) {
	$signingCertificate = Get-SigningCertificate
	try {
		$signingCertificateInfo =
		    Test-SigningCertificateReady $signingCertificate
	} finally {
		$signingCertificate.Dispose()
	}
}

$makeAppxPath = Resolve-MakeAppx
$msixVersion = ConvertTo-MsixVersion $Version
$staging = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("tmux-msix-" + [Guid]::NewGuid().ToString("N"))
$assets = Join-Path $staging "Assets"

try {
	New-Item -ItemType Directory -Force -Path $staging | Out-Null
	Get-ChildItem -LiteralPath $Package -Force | ForEach-Object {
		Copy-Item -LiteralPath $_.FullName -Destination $staging `
		    -Recurse -Force
	}
	New-Item -ItemType Directory -Force -Path $assets | Out-Null
	New-PngAsset (Join-Path $assets "Square44x44Logo.png") 44
	New-PngAsset (Join-Path $assets "Square150x150Logo.png") 150

	$manifestPath = Join-Path $staging "AppxManifest.xml"
	$manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<Package
  xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap5="http://schemas.microsoft.com/appx/manifest/uap/windows10/5"
  xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10"
  xmlns:desktop="http://schemas.microsoft.com/appx/manifest/desktop/windows10"
  xmlns:desktop4="http://schemas.microsoft.com/appx/manifest/desktop/windows10/4"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
  IgnorableNamespaces="uap uap5 uap10 desktop desktop4 rescap">
  <Identity Name="$(Escape-Xml $IdentityName)" Publisher="$(Escape-Xml $Publisher)" Version="$msixVersion" ProcessorArchitecture="$(Escape-Xml $Architecture)" />
  <Properties>
    <DisplayName>$(Escape-Xml $DisplayName)</DisplayName>
    <PublisherDisplayName>$(Escape-Xml $PublisherDisplayName)</PublisherDisplayName>
    <Logo>Assets\Square150x150Logo.png</Logo>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.19041.0" MaxVersionTested="10.0.26100.0" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Applications>
    <Application Id="tmux" Executable="tmux.exe" EntryPoint="Windows.FullTrustApplication" uap10:RuntimeBehavior="packagedClassicApp" uap10:TrustLevel="mediumIL" desktop4:Subsystem="console" desktop4:SupportsMultipleInstances="true">
      <uap:VisualElements DisplayName="$(Escape-Xml $DisplayName)" Description="$(Escape-Xml $Description)" Square150x150Logo="Assets\Square150x150Logo.png" Square44x44Logo="Assets\Square44x44Logo.png" BackgroundColor="transparent" />
      <Extensions>
        <uap5:Extension Category="windows.appExecutionAlias" Executable="tmux.exe" EntryPoint="Windows.FullTrustApplication">
          <uap5:AppExecutionAlias desktop4:Subsystem="console">
            <uap5:ExecutionAlias Alias="$(Escape-Xml $Alias)" />
          </uap5:AppExecutionAlias>
        </uap5:Extension>
      </Extensions>
    </Application>
  </Applications>
  <Capabilities>
    <rescap:Capability Name="runFullTrust" />
  </Capabilities>
</Package>
"@
	Set-Content -LiteralPath $manifestPath -Encoding utf8 -Value $manifest

	$outputDirectory = Split-Path -Parent $Output
	if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
		New-Item -ItemType Directory -Force -Path $outputDirectory |
		    Out-Null
	}
	if (Test-Path -LiteralPath $Output) {
		Remove-Item -LiteralPath $Output -Force
	}
	Invoke-Native $makeAppxPath @("pack", "/d", $staging, "/p", $Output,
	    "/o", "/v") | Out-Null

	$signed = $false
	$signToolPath = ""
	if ($Sign) {
		$signToolPath = Resolve-SignTool
		$signArgs = @("sign", "/fd", "SHA256")
		if (-not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
			$signArgs += @("/tr", $TimestampUrl, "/td", "SHA256")
		}
		if (-not [string]::IsNullOrWhiteSpace($CertificatePath)) {
			$signArgs += @("/f", (Resolve-Path -LiteralPath `
			    $CertificatePath).Path)
			if ($null -ne $CertificatePasswordSecure) {
				try {
					$credential = [System.Management.Automation.PSCredential]::new(
						"_", $CertificatePasswordSecure)
					$plainPassword = $credential.GetNetworkCredential().Password
					$signArgs += @("/p", $plainPassword)
				} finally {
					$plainPassword = $null
				}
			} elseif (-not [string]::IsNullOrWhiteSpace(
			    $CertificatePassword)) {
				$signArgs += @("/p", $CertificatePassword)
			}
		} else {
			$signArgs += @("/sha1",
			    $CertificateThumbprint.Replace(" ", ""))
		}
		$signArgs += $Output
		$output = & $signToolPath @signArgs 2>&1
		if ($LASTEXITCODE -ne 0) {
			$output | Select-Object -First 80 | Where-Object {
				$_ -notmatch '/p\s'
			} | ForEach-Object {
				[Console]::Error.WriteLine($_)
			}
			throw "signtool failed"
		}
		$signed = $true
	}

	$hash = (Get-FileHash -LiteralPath $Output -Algorithm SHA256).
	    Hash.ToLowerInvariant()
	$summaryDirectory = Split-Path -Parent $SummaryPath
	if (-not [string]::IsNullOrWhiteSpace($summaryDirectory)) {
		New-Item -ItemType Directory -Force -Path $summaryDirectory |
		    Out-Null
	}
	[pscustomobject]@{
		GeneratedUtc = [DateTime]::UtcNow.ToString("o")
		Package = $Package
		Msix = $Output
		SHA256 = $hash
		Version = $msixVersion
		IdentityName = $IdentityName
		Publisher = $Publisher
		Architecture = $Architecture
		Alias = $Alias
		MakeAppx = $makeAppxPath
		Signed = $signed
		SignTool = $signToolPath
		SigningCertificate = $signingCertificateInfo
	} | ConvertTo-Json -Depth 3 |
	    Set-Content -LiteralPath $SummaryPath -Encoding ascii

	Write-Host "msix=$Output"
	Write-Host "version=$msixVersion"
	Write-Host "sha256=$hash"
	Write-Host "signed=$signed"
	Write-Host "summary=$SummaryPath"
} finally {
	if (-not $KeepStaging -and (Test-Path -LiteralPath $staging)) {
		$tempRoot = [System.IO.Path]::GetTempPath()
		$stagingFull = [System.IO.Path]::GetFullPath($staging)
		if ($stagingFull.StartsWith($tempRoot,
		    [System.StringComparison]::OrdinalIgnoreCase)) {
			Remove-Item -LiteralPath $stagingFull -Recurse -Force
		}
	}
}
