param([string]$CC = 'gcc')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outDir = Join-Path $root '.codex-tmp/conpty-startup-regression'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'conpty-startup-failure-test.exe'
$compiler = (Get-Command $CC -ErrorAction Stop).Source
$previousPath = $env:Path
try {
    $env:Path = (Split-Path $compiler) + ';' + [Environment]::SystemDirectory
    & $compiler -municode -static-libgcc -I (Join-Path $root 'compat') `
        (Join-Path $PSScriptRoot 'conpty-startup-failure-test.c') `
        (Join-Path $root 'compat/win32-conpty.c') `
        (Join-Path $root 'compat/win32-job.c') `
        (Join-Path $root 'compat/win32-process-tree.c') -o $exe
    if ($LASTEXITCODE -ne 0) { throw 'ConPTY startup regression compilation failed' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "ConPTY startup regression failed: $LASTEXITCODE" }
} finally {
    $env:Path = $previousPath
}
