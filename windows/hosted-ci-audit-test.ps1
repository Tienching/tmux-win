param([string]$AuditScript = (Join-Path $PSScriptRoot 'hosted-ci-audit.ps1'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$global:TmuxAuditFixtureMode = ''
$global:TmuxAuditFixtureSha = 'a' * 40
function git {
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'ls-remote') { return "$global:TmuxAuditFixtureSha`trefs/heads/fixture" }
    throw 'Unexpected git operation in audit fixture'
}
function Invoke-RestMethod {
    param($Uri, $TimeoutSec, $Headers)
    if ($Uri.EndsWith('/actions/workflows')) {
        $items = @()
        if ($global:TmuxAuditFixtureMode -ne 'missing') {
            $items = @([pscustomobject]@{ id=7; name='Windows MinGW';
                path='.github/workflows/windows-mingw.yml'; state='active';
                html_url='https://example.invalid/workflow' })
        }
        return [pscustomobject]@{ workflows=$items }
    }
    $runs = @()
    if ($global:TmuxAuditFixtureMode -ne 'empty') {
        $runs = @([pscustomobject]@{ id=8; name='Windows MinGW'; status='completed';
            conclusion='success'; head_branch='fixture'; head_sha=$global:TmuxAuditFixtureSha;
            html_url='https://example.invalid/run'; created_at='2026-09-22'; updated_at='2026-09-22' })
        if ($global:TmuxAuditFixtureMode -eq 'running') { $runs[0].status='in_progress'; $runs[0].conclusion=$null }
        if ($global:TmuxAuditFixtureMode -eq 'failed') { $runs[0].conclusion='failure' }
        if ($global:TmuxAuditFixtureMode -eq 'other-head') { $runs[0].head_sha='b'*40 }
    }
    return [pscustomobject]@{ workflow_runs=$runs }
}
$outDir = Join-Path $PSScriptRoot ('../.codex-tmp/hosted-audit-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $outDir | Out-Null
try {
    $cases = @{ missing='missing_workflow'; empty='no_run_for_head';
        running='no_green_run_for_head'; failed='no_green_run_for_head';
        'other-head'='no_run_for_head'; green='passed' }
    foreach ($mode in $cases.Keys) {
        $global:TmuxAuditFixtureMode = $mode
        $output = Join-Path $outDir "$mode.json"
        & $AuditScript -Repository 'fixture/repo' `
            -Branch fixture -HeadSha $global:TmuxAuditFixtureSha -Output $output -RequireGreen
        $exit = $LASTEXITCODE
        $audit = Get-Content -LiteralPath $output -Raw | ConvertFrom-Json
        if ($audit.Status -ne $cases[$mode]) { throw "Unexpected $mode status: $($audit.Status)" }
        if (($mode -eq 'green' -and $exit -ne 0) -or
            ($mode -ne 'green' -and $exit -ne 1)) { throw "RequireGreen exit mismatch for $mode" }
        if ($mode -eq 'green' -and $audit.GreenRun.HeadSha -ne $global:TmuxAuditFixtureSha) {
            throw 'Successful run identity mismatch'
        }
    }
    Write-Output 'PASS audit serialization: missing/single workflow, empty/running/failed/other-head/green runs; RequireGreen preserved'
} finally {
    Remove-Variable -Name TmuxAuditFixtureMode,TmuxAuditFixtureSha -Scope Global
}
exit 0
