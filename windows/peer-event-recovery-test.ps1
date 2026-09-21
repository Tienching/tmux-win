param([string]$CC = "gcc")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outDir = Join-Path $root ".codex-tmp/peer-event-test"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
function Invoke-RegressionExecutable([string]$Path) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Path
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
            throw "Test timed out: $Path"
        }
        Write-Output $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($errors) { Write-Output $errors }
        if ($test.ExitCode -ne 0) { throw "Test failed: $Path (exit $($test.ExitCode))" }
    } finally {
        $test.Dispose()
    }
}
$exe = Join-Path $outDir "peer-event-test.exe"
& $CC -O1 -fwhole-program -ffunction-sections -fdata-sections `
    -I $root -I (Join-Path $root "compat") `
    (Join-Path $PSScriptRoot "peer-event-recovery-test.c") `
    '-Wl,--gc-sections' -lws2_32 -levent -o $exe
if ($LASTEXITCODE -ne 0) { throw "Regression test compilation failed" }
Invoke-RegressionExecutable $exe
$integration = Join-Path $outDir "peer-event-integration-test.exe"
& $CC -O1 -fwhole-program -ffunction-sections -fdata-sections `
    -I $root -I (Join-Path $root "compat") `
    (Join-Path $PSScriptRoot "peer-event-integration-test.c") `
    '-Wl,--gc-sections' -lws2_32 -levent -o $integration
if ($LASTEXITCODE -ne 0) { throw "Integration test compilation failed" }
Invoke-RegressionExecutable $integration
