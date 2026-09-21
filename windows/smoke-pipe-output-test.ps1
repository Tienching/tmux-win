param([Parameter(Mandatory=$true)][string]$Tmux)
$ErrorActionPreference = 'Stop'
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path
$TimeoutSeconds = 5
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'smoke-runtime.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Smoke script failed to parse' }
foreach ($name in @('ConvertTo-WindowsArgument', 'Invoke-NamedTmux')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if ($null -eq $definition) { throw "Missing function: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$label = 'codex-pipe-test-' + [guid]::NewGuid().ToString('N')
try {
    Invoke-NamedTmux $label @('new-session', '-d', '-s', 'pipe-test', 'cmd.exe') | Out-Null
    Invoke-NamedTmux $label @('set-environment', '-g', 'TMUX_PIPE_TEST_A', ('A' * 16000)) | Out-Null
    Invoke-NamedTmux $label @('set-environment', '-g', 'TMUX_PIPE_TEST_B', ('B' * 16000)) | Out-Null
    $result = Invoke-NamedTmux $label @('show-environment', '-g')
    if (!$result.Out.Contains('TMUX_PIPE_TEST_A=' + ('A' * 16000)) -or
        !$result.Out.Contains('TMUX_PIPE_TEST_B=' + ('B' * 16000))) {
        throw 'Large environment output was truncated'
    }
    Write-Output 'PASS: redirected output larger than pipe capacity drains without deadlock'
} finally {
    Invoke-NamedTmux $label @('kill-server') | Out-Null
}
