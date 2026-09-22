param([string]$CC = "gcc")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outDir = Join-Path $root '.codex-tmp/ipc-startup-test'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$exe = Join-Path $outDir 'ipc-startup-regression.exe'
& $CC -O1 -fwhole-program -ffunction-sections -fdata-sections `
    -I $root -I (Join-Path $root 'compat') `
    (Join-Path $PSScriptRoot 'ipc-startup-regression.c') `
    '-Wl,--gc-sections' -lws2_32 -ladvapi32 -o $exe
if ($LASTEXITCODE -ne 0) { throw 'IPC startup test compilation failed' }
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $exe
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$test = [Diagnostics.Process]::Start($info)
try {
    $stdout = $test.StandardOutput.ReadToEndAsync()
    $stderr = $test.StandardError.ReadToEndAsync()
    if (-not $test.WaitForExit(15000)) {
        $test.Kill()
        throw 'IPC startup test timed out'
    }
    Write-Output $stdout.GetAwaiter().GetResult()
    $errors = $stderr.GetAwaiter().GetResult()
    if ($errors) { Write-Output $errors }
    if ($test.ExitCode -ne 0) { throw "IPC startup test failed: $($test.ExitCode)" }
} finally { $test.Dispose() }
