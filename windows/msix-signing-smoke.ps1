param(
	[string]$Package = "",
	[string]$Output = "",
	[string]$SummaryPath = "",
	[string]$AuditOutput = "",
	[string]$Publisher = "",
	[switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Dist = Join-Path $Root "dist"

if ([string]::IsNullOrWhiteSpace($Package)) {
	$Package = Join-Path $Dist "tmux-win32-portable"
} elseif (-not [System.IO.Path]::IsPathRooted($Package)) {
	$Package = Join-Path (Get-Location) $Package
}
$Package = (Resolve-Path -LiteralPath $Package).Path

$smokeId = [Guid]::NewGuid().ToString("N")
if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path ([System.IO.Path]::GetTempPath()) `
	    "tmux-msix-signing-smoke-$smokeId.msix"
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

if ([string]::IsNullOrWhiteSpace($AuditOutput)) {
	$AuditOutput = $Output + ".signing-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($AuditOutput)) {
	$AuditOutput = Join-Path (Get-Location) $AuditOutput
}
$AuditOutput = [System.IO.Path]::GetFullPath($AuditOutput)

if ([string]::IsNullOrWhiteSpace($Publisher)) {
	$Publisher = "CN=tmux signing smoke $smokeId"
}

$newSelfSignedCertificate = Get-Command New-SelfSignedCertificate `
    -ErrorAction SilentlyContinue
if ($newSelfSignedCertificate -eq $null) {
	throw "New-SelfSignedCertificate is not available on this host"
}

$certificate = $null
$packageScript = Join-Path $PSScriptRoot "package-msix.ps1"
$auditScript = Join-Path $PSScriptRoot "signing-audit.ps1"

try {
	$certificate = New-SelfSignedCertificate `
	    -Type CodeSigningCert `
	    -Subject $Publisher `
	    -CertStoreLocation "Cert:\CurrentUser\My" `
	    -KeyExportPolicy Exportable `
	    -KeySpec Signature `
	    -KeyUsage DigitalSignature `
	    -NotAfter (Get-Date).AddDays(2)

	& $packageScript `
	    -Package $Package `
	    -Output $Output `
	    -SummaryPath $SummaryPath `
	    -Publisher $Publisher `
	    -Sign `
	    -CertificateThumbprint $certificate.Thumbprint |
	    Out-Null

	& $auditScript `
	    -Msix $Output `
	    -MsixSummary $SummaryPath `
	    -Output $AuditOutput |
	    Out-Null

	$summary = Get-Content -LiteralPath $SummaryPath -Raw |
	    ConvertFrom-Json
	$audit = Get-Content -LiteralPath $AuditOutput -Raw |
	    ConvertFrom-Json

	if (-not [bool]$summary.Signed) {
		throw "MSIX signing smoke summary did not report Signed=true"
	}
	if ($summary.SigningCertificate -eq $null) {
		throw "MSIX signing smoke summary missing SigningCertificate"
	}
	if ($summary.SigningCertificate.Subject -ne $Publisher) {
		throw ("MSIX signing smoke certificate subject mismatch: " +
		    "expected '$Publisher' got '$($summary.SigningCertificate.Subject)'")
	}
	if ($audit.Signer -eq $null) {
		throw "MSIX signing smoke audit did not record an Authenticode signer"
	}
	if ($audit.Signer.Subject -ne $Publisher) {
		throw ("MSIX signing smoke signer mismatch: expected '$Publisher' " +
		    "got '$($audit.Signer.Subject)'")
	}
	if ($audit.SignerSubjectMatchesPublisher -ne $true) {
		throw "MSIX signing smoke signer subject did not match Publisher"
	}
	if ($audit.SummaryHashMatches -ne $true) {
		throw "MSIX signing smoke summary hash mismatch"
	}
	if ($audit.SummaryPublisherMatchesManifest -ne $true) {
		throw "MSIX signing smoke publisher metadata mismatch"
	}
	if (@($audit.MetadataMismatches).Count -ne 0) {
		throw ("MSIX signing smoke metadata mismatches: {0}" -f `
		    (@($audit.MetadataMismatches) -join ";"))
	}
	if ($audit.Status -eq "unsigned") {
		throw "MSIX signing smoke audit still reported unsigned"
	}

	Write-Host "msix_signing_smoke=passed"
	Write-Host "status=$($audit.Status)"
	Write-Host "authenticode_status=$($audit.AuthenticodeStatus)"
	Write-Host "signer_thumbprint=$($audit.Signer.Thumbprint)"
	Write-Host "msix=$Output"
	Write-Host "summary=$SummaryPath"
	Write-Host "signing_audit=$AuditOutput"
} finally {
	if ($certificate -ne $null) {
		$certPath = "Cert:\CurrentUser\My\$($certificate.Thumbprint)"
		Remove-Item -LiteralPath $certPath -Force `
		    -ErrorAction SilentlyContinue
	}
	if (-not $KeepArtifacts) {
		foreach ($path in @($Output, $SummaryPath, $AuditOutput)) {
			if (-not [string]::IsNullOrWhiteSpace($path) -and
			    (Test-Path -LiteralPath $path)) {
				Remove-Item -LiteralPath $path -Force `
				    -ErrorAction SilentlyContinue
			}
		}
	}
}
