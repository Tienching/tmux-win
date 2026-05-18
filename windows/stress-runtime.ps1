param(
	[int]$Iterations = 3,
	[string]$Tmux = "",
	[int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Iterations -lt 1) {
	throw "Iterations must be at least 1"
}

$Smoke = Join-Path $PSScriptRoot "smoke-runtime.ps1"
if (-not (Test-Path -LiteralPath $Smoke)) {
	throw "smoke-runtime.ps1 not found: $Smoke"
}

$started = [Diagnostics.Stopwatch]::StartNew()
for ($i = 1; $i -le $Iterations; $i++) {
	$iteration = [Diagnostics.Stopwatch]::StartNew()
	Write-Host ("[STRESS] iteration {0}/{1}" -f $i, $Iterations)

	$arguments = @(
	    "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $Smoke,
	    "-TimeoutSeconds", $TimeoutSeconds
	)
	if (-not [string]::IsNullOrWhiteSpace($Tmux)) {
		$arguments += @("-Tmux", $Tmux)
	}

	& powershell @arguments
	if ($LASTEXITCODE -ne 0) {
		throw (("smoke-runtime.ps1 failed in iteration {0} with " +
		    "exit code {1}") -f $i, $LASTEXITCODE)
	}
	Write-Host ("[STRESS] iteration {0}/{1} passed in {2:n1}s" -f
	    $i, $Iterations, $iteration.Elapsed.TotalSeconds)
}

Write-Host ("Windows runtime stress passed: {0} iterations in {1:n1}s" -f
    $Iterations, $started.Elapsed.TotalSeconds)
