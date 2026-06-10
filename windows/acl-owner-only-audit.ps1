# acl-owner-only-audit.ps1
# Verifies that tmux endpoint files have DACLs containing only the current
# user SID (no inherited or extraneous ACEs). Run on a Windows host where
# tmux-win has created endpoint files.
#
# Exit codes: 0 = all endpoint files pass, 1 = one or more failures.

param(
    [string]$EndpointDir = "$env:LOCALAPPDATA\tmux"
)

$errorCount = 0
$passCount  = 0

# Get the current user SID string for comparison.
$currentUserSid = ([System.Security.Principal.WindowsIdentity]::GetCurrent()).User.Value

if (-not $currentUserSid) {
    Write-Host "FAIL: could not determine current user SID" -ForegroundColor Red
    exit 1
}

Write-Host "Current user SID: $currentUserSid"
Write-Host "Scanning: $EndpointDir"
Write-Host ""

# Find all .endpoint files under the endpoint directory.
$files = @()
if (Test-Path $EndpointDir) {
    $files = @(Get-ChildItem -Path $EndpointDir -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -eq ".endpoint" })
}

if ($files.Count -eq 0) {
    Write-Host "WARN: no .endpoint files found under $EndpointDir" -ForegroundColor Yellow
    exit 0
}

foreach ($file in $files) {
    $acl = $null
    try {
        $acl = Get-Acl -LiteralPath $file.FullName -ErrorAction Stop
    } catch {
        Write-Host "FAIL: $($file.FullName) - cannot read ACL: $($_.Exception.Message)" -ForegroundColor Red
        $errorCount++
        continue
    }

    $accessRules = @($acl.Access | Where-Object {
        $_.AccessControlType -eq "Allow"
    })

    $failed = $false

    # There must be at least one allow ACE.
    if ($accessRules.Count -eq 0) {
        Write-Host "FAIL: $($file.FullName) - no allow ACEs in DACL" -ForegroundColor Red
        $failed = $true
    }

    # Every allow ACE must reference the current user SID only.
    foreach ($rule in $accessRules) {
        $sid = $rule.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value
        if ($sid -ne $currentUserSid) {
            Write-Host "FAIL: $($file.FullName) - unexpected SID in ACE: $sid (expected $currentUserSid)" -ForegroundColor Red
            $failed = $true
        }
    }

    # Check that no inherited ACEs are present (all ACEs should be explicit).
    $inheritedRules = @($accessRules | Where-Object { $_.IsInherited -eq $true })
    if ($inheritedRules.Count -gt 0) {
        Write-Host "FAIL: $($file.FullName) - found $($inheritedRules.Count) inherited ACE(s); expected explicit only" -ForegroundColor Red
        $failed = $true
    }

    if (-not $failed) {
        Write-Host "PASS: $($file.FullName)" -ForegroundColor Green
        $passCount++
    } else {
        $errorCount++
    }
}

Write-Host ""
Write-Host "Results: $passCount passed, $errorCount failed (of $($files.Count) files)"

if ($errorCount -gt 0) {
    exit 1
}
exit 0
