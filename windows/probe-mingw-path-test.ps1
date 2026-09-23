$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'probe-mingw.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Probe script failed to parse' }
foreach ($name in @('Resolve-CommandPath', 'Get-Msys2Prefixes')) {
    $definition = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $true)
    if ($null -eq $definition) { throw "Missing function: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$CC = ''
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('tmux-probe-path-' + [guid]::NewGuid())
$originalPath = $env:PATH
$originalLocation = Get-Location
try {
    $prefix = Join-Path $testRoot 'UcRt64'
    New-Item -ItemType Directory -Path (Join-Path $testRoot 'bin'), (Join-Path $prefix 'bin') -Force | Out-Null
    Set-Location $testRoot
    # A relative bin used to throw; empty/root entries must be harmless, and
    # legitimate prefixes must remain case-insensitive and deduplicated.
    $env:PATH = 'bin;;' + [IO.Path]::GetPathRoot($testRoot) + ';' + (Join-Path $prefix 'bin') + ';' + (Join-Path $prefix 'bin')
    $prefixes = @(Get-Msys2Prefixes)
    if (@($prefixes | Where-Object { $_ -ieq $prefix }).Count -ne 1) {
        throw 'Expected exactly one recognized UCRT prefix'
    }
    Write-Output 'PASS: relative bin, empty/root PATH entries, mixed-case prefix, deduplication'
} finally {
    $env:PATH = $originalPath
    Set-Location $originalLocation
    # Delete only the unique test directory created above.
    if ((Split-Path -Leaf $testRoot) -like 'tmux-probe-path-*' -and
        (Split-Path -Parent $testRoot).TrimEnd('\') -eq ([IO.Path]::GetTempPath()).TrimEnd('\')) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
