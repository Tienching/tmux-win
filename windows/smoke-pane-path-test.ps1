$ErrorActionPreference = 'Stop'
# Load just the helper from the actual smoke test, not its server side effects.
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'smoke-runtime.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Smoke script parse failure' }
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Wait-PaneCurrentPath'
}, $true)
if (-not $function) { throw 'Missing actual Wait-PaneCurrentPath helper' }
Invoke-Expression $function.Extent.Text
function Resolve-SmokePath([string]$Path) { (Resolve-Path -LiteralPath $Path).Path }
$script:mode = ''; $script:reads = 0
$script:expected = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$script:other = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
function Invoke-SmokeTmux([string[]]$Arguments) {
    if ($Arguments[0] -eq 'display-message') {
        $script:reads++
        $id = '%42'; $dead = '0'; $path = $script:expected
        if ($script:mode -eq 'wrong-id') { $id = '%1' }
        if ($script:mode -eq 'dead') { $dead = '1' }
        if ($script:mode -eq 'wrong-path' -or
            ($script:mode -eq 'delay' -and $script:reads -eq 1)) { $path = $script:other }
        return [pscustomobject]@{ Out = "$id|$dead|cmd.exe|$path" }
    }
    return [pscustomobject]@{ Out = 'bounded diagnostic fixture' }
}
foreach ($mode in @('wrong-id', 'dead', 'wrong-path')) {
    $script:mode = $mode; $script:reads = 0; $failure = $null
    try { Wait-PaneCurrentPath '%42' $script:expected 200 | Out-Null }
    catch { $failure = $_.Exception.Message }
    if (-not $failure) { throw "Incorrectly accepted $mode" }
    if ($mode -ne 'wrong-path' -and $script:reads -ne 1) {
        throw 'Missing or dead target must fail immediately'
    }
}
$script:mode = 'delay'; $script:reads = 0
$actual = Wait-PaneCurrentPath '%42' $script:expected
if ($actual -ne $script:expected -or $script:reads -ne 2) {
    throw 'Live same-pane path convergence did not pass'
}
Write-Output 'PASS exact pane identity, immediate missing/dead failure, bounded wrong path and convergence'
