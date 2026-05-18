param(
	[string]$Output = "",
	[int]$MaxEntries = 200,
	[switch]$RequireClean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Dist = Join-Path $Root "dist"

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Dist "source-state-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

if ($MaxEntries -lt 0) {
	$MaxEntries = 0
}

function Invoke-GitText([string[]]$Arguments) {
	$oldErrorActionPreference = $ErrorActionPreference
	try {
		$ErrorActionPreference = "Continue"
		$output = & git @Arguments 2>$null
		if ($LASTEXITCODE -ne 0) {
			return $null
		}
		return $output
	} finally {
		$ErrorActionPreference = $oldErrorActionPreference
	}
}

function ConvertTo-Sha256Hex([string]$Text) {
	$sha = [System.Security.Cryptography.SHA256]::Create()
	try {
		$bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
		$hash = $sha.ComputeHash($bytes)
		return ([System.BitConverter]::ToString($hash) -replace "-", "").
		    ToLowerInvariant()
	} finally {
		$sha.Dispose()
	}
}

function Get-RelativeFileHash([string]$Path) {
	$full = Join-Path $Root $Path
	if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
		return [pscustomobject]@{
		    Path = $Path
		    Exists = $false
		    Size = 0
		    SHA256 = ""
		}
	}
	$item = Get-Item -LiteralPath $full
	return [pscustomobject]@{
	    Path = $Path
	    Exists = $true
	    Size = $item.Length
	    SHA256 = (Get-FileHash -LiteralPath $full -Algorithm SHA256).
		Hash.ToLowerInvariant()
	}
}

$inside = Invoke-GitText @("rev-parse", "--is-inside-work-tree")
if ($inside -eq $null -or [string]$inside -ne "true") {
	throw "not inside a git work tree: $Root"
}

$headSha = [string](Invoke-GitText @("rev-parse", "HEAD"))
$branch = [string](Invoke-GitText @("branch", "--show-current"))
$statusLines = @(Invoke-GitText @(
    "status", "--porcelain=v1", "--untracked-files=all"))

$tracked = [System.Collections.Generic.List[object]]::new()
$untracked = [System.Collections.Generic.List[object]]::new()
foreach ($line in $statusLines) {
	if ([string]::IsNullOrWhiteSpace($line) -or $line.Length -lt 4) {
		continue
	}
	$code = $line.Substring(0, 2)
	$path = $line.Substring(3)
	$entry = [pscustomobject]@{
	    Code = $code
	    Path = $path
	}
	if ($code -eq "??") {
		$untracked.Add($entry)
	} else {
		$tracked.Add($entry)
	}
}

$dirty = ($tracked.Count + $untracked.Count) -gt 0
$trackedDiffText = (Invoke-GitText @("diff", "--binary", "HEAD", "--")) -join "`n"
$trackedDiffSha256 = ConvertTo-Sha256Hex $trackedDiffText
$untrackedFileHashes = [System.Collections.Generic.List[object]]::new()
foreach ($entry in $untracked) {
	$untrackedFileHashes.Add((Get-RelativeFileHash $entry.Path))
}
$fingerprintLines = [System.Collections.Generic.List[string]]::new()
$fingerprintLines.Add("head=$headSha")
$fingerprintLines.Add("status=$($statusLines -join '|')")
$fingerprintLines.Add("tracked_diff=$trackedDiffSha256")
foreach ($file in @($untrackedFileHashes.ToArray() | Sort-Object Path)) {
	$fingerprintLines.Add(("untracked={0}:{1}:{2}:{3}" -f `
	    $file.Path, $file.Exists, $file.Size, $file.SHA256))
}
$sourceStateFingerprint =
    ConvertTo-Sha256Hex ($fingerprintLines.ToArray() -join "`n")
$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Root = $Root
	HeadSha = $headSha
	Branch = $branch
	IsDirty = $dirty
	TrackedChangedCount = $tracked.Count
	UntrackedCount = $untracked.Count
	TrackedDiffSha256 = $trackedDiffSha256
	SourceStateFingerprint = $sourceStateFingerprint
	RecordedEntryLimit = $MaxEntries
	TrackedChanges = @($tracked.ToArray() | Select-Object -First $MaxEntries)
	UntrackedFiles = @($untracked.ToArray() | Select-Object -First $MaxEntries)
	UntrackedFileHashes = @($untrackedFileHashes.ToArray() |
	    Select-Object -First $MaxEntries)
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summary | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "source_state_audit=$Output"
Write-Host "head_sha=$headSha"
Write-Host "dirty=$dirty"
Write-Host "tracked_changed=$($tracked.Count)"
Write-Host "untracked=$($untracked.Count)"
Write-Host "source_state_fingerprint=$sourceStateFingerprint"

if ($RequireClean -and $dirty) {
	exit 1
}
