param(
    [Parameter(Mandatory = $true)][string]$Tmux,
    [string]$OtherPublisherFile
)

$ErrorActionPreference = 'Stop'
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path
$runtime = Split-Path -Parent $Tmux
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('tmux-conpty-trust-' + [Guid]::NewGuid().ToString('N'))
$bundle = Join-Path $testRoot 'conpty'
$testTmux = Join-Path $testRoot (Split-Path -Leaf $Tmux)
$components = 'conpty.dll', 'OpenConsole.exe'

function Test-BundledBackend([string]$Scenario, [bool]$ExpectBundled) {
    $socket = 'conpty-trust-' + [Guid]::NewGuid().ToString('N')
    $server = $null
    try {
        & $testTmux -L $socket -f NUL new-session -d -s trust
        if ($LASTEXITCODE -ne 0) { throw "$Scenario could not start a pane." }
        $serverId = & $testTmux -L $socket display-message -p '#{pid}'
        if ($LASTEXITCODE -ne 0) { throw "$Scenario could not get the test server PID." }
        $server = Get-Process -Id ([int]$serverId)
        $loaded = $server.Modules.FileName -contains (Join-Path $bundle 'conpty.dll')
        if ($loaded -ne $ExpectBundled) {
            throw "$Scenario expected bundled ConPTY=$ExpectBundled, observed $loaded."
        }
        Write-Output "$Scenario passed."
    } finally {
        & $testTmux -L $socket kill-server
        if ($null -ne $server) {
            try {
                if (!$server.WaitForExit(5000)) { throw "$Scenario test server did not exit." }
            } finally {
                $server.Dispose()
            }
        }
    }
}

New-Item -ItemType Directory -Path $bundle -Force | Out-Null
try {
    Copy-Item -LiteralPath $Tmux -Destination $testTmux
    Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File | Copy-Item -Destination $testRoot
    foreach ($component in $components) {
        $source = Join-Path $runtime "conpty/$component"
        if ((Get-AuthenticodeSignature -LiteralPath $source).Status -ne 'Valid') {
            throw "The fixture must have a valid signature: $source"
        }
        Copy-Item -LiteralPath $source -Destination $bundle
    }

    Test-BundledBackend 'Genuine signed pair' $true

    foreach ($component in $components) {
        $path = Join-Path $bundle $component
        $bytes = [IO.File]::ReadAllBytes($path)
        # Change a byte in the signed DOS stub without changing the PE headers.
        $bytes[0x40] = $bytes[0x40] -bxor 1
        [IO.File]::WriteAllBytes($path, $bytes)
        if ((Get-AuthenticodeSignature -LiteralPath $path).Status -ne 'HashMismatch') {
            throw "Expected a signature hash mismatch for $component."
        }
        Test-BundledBackend "Tampered $component" $false
        Copy-Item -LiteralPath (Join-Path $runtime "conpty/$component") -Destination $path -Force
    }

    $hostPath = Join-Path $bundle 'OpenConsole.exe'
    $bytes = [IO.File]::ReadAllBytes($hostPath)
    $optionalHeader = [BitConverter]::ToInt32($bytes, 0x3c) + 24
    $dataDirectories = if ([BitConverter]::ToUInt16($bytes, $optionalHeader) -eq 0x20b) { 112 } else { 96 }
    # Remove the certificate-table entry from this disposable PE copy.
    [Array]::Clear($bytes, $optionalHeader + $dataDirectories + 4 * 8, 8)
    [IO.File]::WriteAllBytes($hostPath, $bytes)
    if ((Get-AuthenticodeSignature -LiteralPath $hostPath).Status -ne 'NotSigned') {
        throw 'Expected an unsigned host fixture.'
    }
    Test-BundledBackend 'Unsigned host' $false

    if ($OtherPublisherFile) {
        $other = Get-AuthenticodeSignature -LiteralPath $OtherPublisherFile
        if ($other.Status -ne 'Valid' -or $other.SignerCertificate.Subject -like 'CN=Microsoft Corporation,*') {
            throw 'Expected a valid Authenticode signature from another publisher.'
        }
        Copy-Item -LiteralPath $OtherPublisherFile -Destination $hostPath -Force
        Test-BundledBackend 'Host signed by another publisher' $false
    }

    Remove-Item -LiteralPath $hostPath
    Test-BundledBackend 'Missing host' $false
} finally {
    Get-ChildItem -LiteralPath $bundle -File | Remove-Item
    Remove-Item -LiteralPath $bundle
    Get-ChildItem -LiteralPath $testRoot -File | Remove-Item
    Remove-Item -LiteralPath $testRoot
}
