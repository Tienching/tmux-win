param([string]$CC = 'gcc')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outDir = Join-Path $root '.codex-tmp/process-snapshot-test'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'process-snapshot-test.exe'
& $CC -O1 -fwhole-program -ffunction-sections -fdata-sections `
    -I $root -I (Join-Path $root 'compat') `
    (Join-Path $PSScriptRoot 'process-snapshot-test.c') `
    '-Wl,--gc-sections' -lws2_32 -levent -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Process snapshot regression compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Process snapshot regression failed' }
