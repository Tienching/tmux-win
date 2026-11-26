param([string]$CC = 'gcc')

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$outDir = Join-Path $root '.codex-tmp/win32-key-test'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'win32-key-test.exe'
$sources = 'windows/win32-key-test.c', 'compat/win32-key.c', 'utf8.c', 'log.c', 'xmalloc.c' |
    ForEach-Object { Join-Path $root $_ }
& $CC -std=gnu99 -DHAVE_EVENT2_EVENT_H -I $root -I (Join-Path $root 'compat') `
    -O1 -flto -fwhole-program @sources -luser32 -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Win32 key test compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Win32 key test failed' }
