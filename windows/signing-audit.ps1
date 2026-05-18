param(
	[string]$Msix = "",
	[string]$MsixSummary = "",
	[string]$Output = "",
	[switch]$RequireTrusted
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Dist = Join-Path $Root "dist"

if ([string]::IsNullOrWhiteSpace($Msix)) {
	$Msix = Join-Path $Dist "tmux-win32.msix"
} elseif (-not [System.IO.Path]::IsPathRooted($Msix)) {
	$Msix = Join-Path (Get-Location) $Msix
}
$Msix = [System.IO.Path]::GetFullPath($Msix)
if (-not (Test-Path -LiteralPath $Msix)) {
	throw "MSIX not found: $Msix"
}

if ([string]::IsNullOrWhiteSpace($MsixSummary)) {
	$MsixSummary = $Msix + ".json"
} elseif (-not [System.IO.Path]::IsPathRooted($MsixSummary)) {
	$MsixSummary = Join-Path (Get-Location) $MsixSummary
}
$MsixSummary = [System.IO.Path]::GetFullPath($MsixSummary)

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Dist "signing-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

$summary = $null
if (Test-Path -LiteralPath $MsixSummary) {
	$summary = Get-Content -LiteralPath $MsixSummary -Raw |
	    ConvertFrom-Json
}

function Read-MsixManifestPublisher([string]$Path) {
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	$archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
	try {
		$entry = @($archive.Entries | Where-Object {
		    $_.FullName -ieq "AppxManifest.xml"
		} | Select-Object -First 1)
		if ($entry.Count -eq 0) {
			throw "AppxManifest.xml not found in MSIX"
		}
		$reader = [System.IO.StreamReader]::new($entry[0].Open())
		try {
			[xml]$manifest = $reader.ReadToEnd()
		} finally {
			$reader.Dispose()
		}
		$identity = $manifest.SelectSingleNode(
		    "/*[local-name()='Package']/*[local-name()='Identity']")
		if ($identity -eq $null) {
			throw "Identity element not found in AppxManifest.xml"
		}
		return [string]$identity.Publisher
	} finally {
		$archive.Dispose()
	}
}

function Get-CertificateEnhancedKeyUsages(
    [System.Security.Cryptography.X509Certificates.X509Certificate2]$Certificate) {
	$items = [System.Collections.Generic.List[object]]::new()
	foreach ($extension in @($Certificate.Extensions)) {
		if ($extension.Oid.Value -ne "2.5.29.37") {
			continue
		}
		$eku = [System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new(
		    $extension, $extension.Critical)
		foreach ($oid in @($eku.EnhancedKeyUsages)) {
			$items.Add([pscustomobject]@{
			    Oid = [string]$oid.Value
			    FriendlyName = [string]$oid.FriendlyName
			})
		}
	}
	return $items.ToArray()
}

function Get-CodeSigningCertificateCandidates([string]$Publisher) {
	$codeSigningOid = "1.3.6.1.5.5.7.3.3"
	$candidates = [System.Collections.Generic.List[object]]::new()
	$errors = [System.Collections.Generic.List[object]]::new()
	foreach ($store in @("Cert:\CurrentUser\My", "Cert:\LocalMachine\My")) {
		try {
			$certificates = @(Get-ChildItem -Path $store `
			    -ErrorAction Stop)
		} catch {
			$errors.Add([pscustomobject]@{
			    Store = $store
			    Error = $_.Exception.Message
			})
			continue
		}
		foreach ($certificate in $certificates) {
			$ekus = @(Get-CertificateEnhancedKeyUsages $certificate)
			$ekuOids = @($ekus | ForEach-Object { $_.Oid })
			if ($ekuOids -notcontains $codeSigningOid) {
				continue
			}
			$subjectMatchesPublisher =
			    -not [string]::IsNullOrWhiteSpace($Publisher) -and
			    $certificate.Subject -eq $Publisher
			$now = [DateTime]::UtcNow
			$isTimeValid =
			    $certificate.NotBefore.ToUniversalTime() -le $now -and
			    $certificate.NotAfter.ToUniversalTime() -ge $now
			$candidates.Add([pscustomobject]@{
			    Store = $store
			    Subject = $certificate.Subject
			    Issuer = $certificate.Issuer
			    Thumbprint = $certificate.Thumbprint
			    NotBefore = $certificate.NotBefore.ToString("o")
			    NotAfter = $certificate.NotAfter.ToString("o")
			    HasPrivateKey = [bool]$certificate.HasPrivateKey
			    IsTimeValid = $isTimeValid
			    SubjectMatchesPublisher = $subjectMatchesPublisher
			    EnhancedKeyUsages = $ekus
			})
		}
	}
	return [pscustomobject]@{
	    Candidates = $candidates.ToArray()
	    StoreErrors = $errors.ToArray()
	}
}

$hash = (Get-FileHash -LiteralPath $Msix -Algorithm SHA256).
    Hash.ToLowerInvariant()
$signature = Get-AuthenticodeSignature -LiteralPath $Msix
$signer = $signature.SignerCertificate
$chainStatuses = @()
if ($signer -ne $null) {
	$chain = [System.Security.Cryptography.X509Certificates.X509Chain]::new()
	try {
		$chain.ChainPolicy.RevocationMode =
		    [System.Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
		[void]$chain.Build($signer)
		$chainStatuses = @($chain.ChainStatus | ForEach-Object {
		    [pscustomobject]@{
			Status = [string]$_.Status
			Information = $_.StatusInformation.Trim()
		    }
		})
	} finally {
		$chain.Dispose()
	}
}

$summarySigned = $false
$summaryHash = ""
$summaryHashMatches = $null
$summaryPublisher = ""
$summaryPublisherMatchesManifest = $null
if ($summary -ne $null -and
    $summary.PSObject.Properties.Name -contains "Signed") {
	$summarySigned = [bool]$summary.Signed
}
if ($summary -ne $null -and
    $summary.PSObject.Properties.Name -contains "SHA256") {
	$summaryHash = ([string]$summary.SHA256).ToLowerInvariant()
	$summaryHashMatches = $summaryHash -eq $hash
}
if ($summary -ne $null -and
    $summary.PSObject.Properties.Name -contains "Publisher") {
	$summaryPublisher = [string]$summary.Publisher
}

$manifestPublisher = ""
$manifestReadError = ""
try {
	$manifestPublisher = Read-MsixManifestPublisher $Msix
} catch {
	$manifestReadError = $_.Exception.Message
}
if (-not [string]::IsNullOrWhiteSpace($summaryPublisher) -and
    -not [string]::IsNullOrWhiteSpace($manifestPublisher)) {
	$summaryPublisherMatchesManifest =
	    $summaryPublisher -eq $manifestPublisher
}

$signerSubjectMatchesPublisher = $null
if ($signer -ne $null -and
    -not [string]::IsNullOrWhiteSpace($manifestPublisher)) {
	$signerSubjectMatchesPublisher =
	    $signer.Subject -eq $manifestPublisher
}

$metadataMismatches = [System.Collections.Generic.List[string]]::new()
if ($summaryHashMatches -eq $false) {
	$metadataMismatches.Add("summary SHA256 does not match MSIX")
}
if ($summaryPublisherMatchesManifest -eq $false) {
	$metadataMismatches.Add("summary Publisher does not match manifest Publisher")
}
if ($signerSubjectMatchesPublisher -eq $false) {
	$metadataMismatches.Add("signer subject does not match manifest Publisher")
}
if ($signer -ne $null -and -not $summarySigned) {
	$metadataMismatches.Add("MSIX has signer but summary Signed is false")
}
if ($signer -eq $null -and $summarySigned) {
	$metadataMismatches.Add("summary Signed is true but MSIX has no signer")
}
if ($signer -ne $null -and
    [string]::IsNullOrWhiteSpace($manifestPublisher)) {
	$metadataMismatches.Add("manifest Publisher unavailable")
}

$candidateAudit = Get-CodeSigningCertificateCandidates $manifestPublisher
$signingCertificateCandidates = @($candidateAudit.Candidates)
$publisherMatchingCandidateCount =
    @($signingCertificateCandidates | Where-Object {
	$_.SubjectMatchesPublisher
    }).Count
$usablePublisherMatchingCandidateCount =
    @($signingCertificateCandidates | Where-Object {
	$_.SubjectMatchesPublisher -and $_.HasPrivateKey -and $_.IsTimeValid
    }).Count

$status = if ($signature.Status -eq "Valid" -and $signer -ne $null -and
    $metadataMismatches.Count -eq 0) {
	"trusted"
} elseif ($signature.Status -eq "Valid" -and $signer -ne $null) {
	"metadata_mismatch"
} elseif ($signer -ne $null) {
	"untrusted_or_invalid"
} else {
	"unsigned"
}

$audit = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Status = $status
	Msix = $Msix
	SHA256 = $hash
	MsixSummary = $MsixSummary
	MsixSummarySigned = $summarySigned
	SummarySHA256 = $summaryHash
	SummaryHashMatches = $summaryHashMatches
	SummaryPublisher = $summaryPublisher
	ManifestPublisher = $manifestPublisher
	ManifestReadError = $manifestReadError
	SummaryPublisherMatchesManifest = $summaryPublisherMatchesManifest
	SignerSubjectMatchesPublisher = $signerSubjectMatchesPublisher
	MetadataMismatches = $metadataMismatches.ToArray()
	CodeSigningCandidateCount = $signingCertificateCandidates.Count
	PublisherMatchingCandidateCount = $publisherMatchingCandidateCount
	UsablePublisherMatchingCandidateCount =
	    $usablePublisherMatchingCandidateCount
	CodeSigningCertificateCandidates = $signingCertificateCandidates
	CertificateStoreErrors = $candidateAudit.StoreErrors
	AuthenticodeStatus = [string]$signature.Status
	AuthenticodeStatusMessage = [string]$signature.StatusMessage
	Signer = $(if ($signer -ne $null) {
	    [pscustomobject]@{
		Subject = $signer.Subject
		Issuer = $signer.Issuer
		Thumbprint = $signer.Thumbprint
		NotBefore = $signer.NotBefore.ToString("o")
		NotAfter = $signer.NotAfter.ToString("o")
	    }
	} else { $null })
	ChainStatus = $chainStatuses
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$audit | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "signing_audit=$Output"
Write-Host "status=$status"
Write-Host "authenticode_status=$($signature.Status)"
if ($signer -ne $null) {
	Write-Host "signer_thumbprint=$($signer.Thumbprint)"
}
if ($RequireTrusted -and $status -ne "trusted") {
	exit 1
}
