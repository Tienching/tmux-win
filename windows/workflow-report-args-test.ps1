param([string]$Workflow = (Join-Path $PSScriptRoot '../.github/workflows/windows-mingw.yml'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$yaml = Get-Content -LiteralPath $Workflow -Raw
function Get-WorkflowBody([string]$Name) {
    $pattern = '(?ms)^      - name: ' + [regex]::Escape($Name) +
        '\r?\n        shell: powershell\r?\n        run: \|\r?\n(?<body>.*?)(?=^      - |\z)'
    $match = [regex]::Match($yaml, $pattern)
    if (-not $match.Success) { throw "Cannot extract actual workflow step $Name" }
    $body = [regex]::Replace($match.Groups['body'].Value, '(?m)^          ', '')
    return $body.Replace('${{ github.repository }}', 'fixture/repo').Replace('${{ github.run_id }}', '123')
}
# Use each real script's parameter declaration, without running its audit body.
foreach ($entry in @(@('completion-audit.ps1', 'Invoke-CompletionFixture'),
    @('verify-release-artifacts.ps1', 'Invoke-VerifyFixture'))) {
    $tokens=$null; $errors=$null
    $ast=[System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot $entry[0]), [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw 'Audit script parse failed' }
    $definition = 'function ' + $entry[1] + ' { [CmdletBinding()] ' +
        $ast.ParamBlock.Extent.Text + "`n" + 'return [pscustomobject]@{ Values=$PSBoundParameters } }'
    Invoke-Expression $definition
}
$completion = (Get-WorkflowBody 'Write Windows completion audit').Replace(
    '.\windows\completion-audit.ps1', 'Invoke-CompletionFixture')
$verify = (Get-WorkflowBody 'Verify Windows extended evidence').Replace(
    '.\windows\verify-release-artifacts.ps1', 'Invoke-VerifyFixture')
function Test-Path { param($Path) return $script:linuxAvailable }
foreach ($linuxAvailable in @($false, $true)) {
    $script:linuxAvailable=$linuxAvailable
    $result = & ([scriptblock]::Create($completion))
    $expected=@{ SigningSummary='.\dist\signing-audit.json';
        IpcBoundarySummary='.\dist\ipc-boundary-audit.json';
        HostedCiSummary='.\dist\hosted-ci-audit.json';
        SourceStateSummary='.\dist\source-state-audit.json';
        HostedCiRunUrl='https://github.com/fixture/repo/actions/runs/123' }
    if ($linuxAvailable) {
        $expected.LinuxParitySummary='.\dist\linux-parity-matrix.json'
        $expected.LinuxBehaviorSummary='.\dist\linux-behavior-parity.json'
    }
    if ($result.Values.Count -ne $expected.Count) { throw 'Completion parameter count mismatch' }
    foreach ($key in $expected.Keys) {
        if (-not $result.Values.ContainsKey($key) -or $result.Values[$key] -ne $expected[$key]) {
            throw "Completion named parameter mismatch: $key"
        }
    }
    $result = & ([scriptblock]::Create($verify))
    $required=@('RequireMsix','RequireSigningAudit','RequireCompletionAudit',
        'RequireIpcBoundaryAudit','RequireHostedCiAudit','RequireSourceStateAudit')
    if ($linuxAvailable) { $required+=@('RequireLinuxParity','RequireLinuxBehaviorParity') }
    if ($result.Values.Count -ne $required.Count) { throw 'Verification parameter count mismatch' }
    foreach ($key in $required) {
        if (-not $result.Values.ContainsKey($key) -or -not $result.Values[$key].IsPresent) {
            throw "Required verification switch not bound: $key"
        }
    }
}
Write-Output 'PASS actual workflow named parameter/switch binding with and without optional Linux evidence'
