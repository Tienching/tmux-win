param(
	[string]$Tmux = "",
	[int]$Iterations = 3,
	[int]$TimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "dist\tmux-win32-portable\tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

function ConvertTo-WindowsArgument([string]$Argument) {
	$quote = [string][char]34
	$backslash = [string][char]92
	$needsQuotes = [string]::IsNullOrEmpty($Argument) -or
	    $Argument.IndexOfAny([char[]]@(' ', "`t", '"')) -ne -1
	if (-not $needsQuotes) {
		return $Argument
	}
	$result = $quote
	$slashes = 0
	foreach ($ch in $Argument.ToCharArray()) {
		if ($ch -eq [char]92) {
			$slashes++
			continue
		}
		if ($ch -eq [char]34) {
			$result += $backslash * ($slashes * 2 + 1)
			$result += $quote
			$slashes = 0
			continue
		}
		if ($slashes -gt 0) {
			$result += $backslash * $slashes
			$slashes = 0
		}
		$result += $ch
	}
	if ($slashes -gt 0) {
		$result += $backslash * ($slashes * 2)
	}
	$result += $quote
	return $result
}

function Invoke-IpcTmux([string]$ServerName, [string[]]$Arguments,
    [switch]$AllowFailure) {
	$allArguments = @("-L", $ServerName, "-f", "NUL") + $Arguments
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($allArguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	$process = [System.Diagnostics.Process]::Start($psi)
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		try {
			$process.Kill()
		} catch {
		}
		throw "tmux timed out: $($Arguments -join ' ')"
	}
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Out = $process.StandardOutput.ReadToEnd()
		Err = $process.StandardError.ReadToEnd()
	}
	if (-not $AllowFailure -and $result.ExitCode -ne 0) {
		throw @"
tmux failed: $($Arguments -join ' ')
exit code: $($result.ExitCode)
stdout:
$($result.Out)
stderr:
$($result.Err)
"@
	}
	return $result
}

function Assert-EndpointAcl([string]$Path) {
	$current = [System.Security.Principal.WindowsIdentity]::GetCurrent()
	$acl = Get-Acl -LiteralPath $Path
	$rules = @($acl.Access)
	$userRule = $rules | Where-Object {
		$_.IdentityReference.Value -eq $current.Name -and
		$_.AccessControlType -eq "Allow" -and
		$_.FileSystemRights.ToString() -like "*FullControl*"
	}
	if (@($userRule).Count -eq 0) {
		throw "endpoint ACL does not grant current user FullControl"
	}
	$broad = $rules | Where-Object {
		$_.AccessControlType -eq "Allow" -and
		($_.IdentityReference.Value -match '(\\|^)(Everyone|Users|Authenticated Users)$')
	}
	if (@($broad).Count -ne 0) {
		throw "endpoint ACL grants broad access: $($broad.IdentityReference)"
	}
}

function Assert-EndpointContent([string]$Path) {
	$lines = @(Get-Content -LiteralPath $Path)
	if ($lines.Count -ne 3) {
		throw "endpoint should contain exactly 3 lines"
	}
	if ($lines[0] -ne "tmux-win32-ipc-v1") {
		throw "endpoint magic mismatch: $($lines[0])"
	}
	if ($lines[1] -notmatch '^[0-9]+$') {
		throw "endpoint port is not numeric: $($lines[1])"
	}
	if ($lines[2] -notmatch '^[0-9a-f]{64}$') {
		throw "endpoint token is not a 32-byte lowercase hex token"
	}
}

function Assert-BadTokenRejected([string]$Path) {
	$lines = @(Get-Content -LiteralPath $Path)
	$port = [int]$lines[1]
	$client = [System.Net.Sockets.TcpClient]::new()
	$client.ReceiveTimeout = 2000
	try {
		$client.Connect("127.0.0.1", $port)
		$stream = $client.GetStream()
		$badToken = [byte[]]::new(32)
		$stream.Write($badToken, 0, $badToken.Length)
		$buffer = [byte[]]::new(1)
		try {
			$read = $stream.Read($buffer, 0, 1)
			if ($read -ne 0) {
				throw "server returned data after a bad IPC token"
			}
		} catch [System.IO.IOException] {
			$socketError = $_.Exception.InnerException
			if ($socketError -is [System.Net.Sockets.SocketException] -and
			    $socketError.SocketErrorCode -eq
			    [System.Net.Sockets.SocketError]::TimedOut) {
				throw "server kept connection open after a bad IPC token"
			}
		}
	} finally {
		$client.Close()
	}
}

if ($Iterations -lt 1) {
	throw "-Iterations must be at least 1"
}

$endpointRoot = Join-Path $env:LOCALAPPDATA "tmux"
for ($i = 1; $i -le $Iterations; $i++) {
	$serverName = "ipc-acl-" + [Guid]::NewGuid().ToString("N")
	$endpoint = Join-Path $endpointRoot ($serverName + ".endpoint")
	Write-Host ("[IPC-ACL] iteration {0}/{1}" -f $i, $Iterations)
	try {
		Invoke-IpcTmux $serverName @(
		    "new-session", "-d", "-s", "acl", "cmd.exe") | Out-Null
		if (-not (Test-Path -LiteralPath $endpoint)) {
			throw "endpoint not created: $endpoint"
		}
		Assert-EndpointAcl $endpoint
		Assert-EndpointContent $endpoint
		Assert-BadTokenRejected $endpoint
		$good = Invoke-IpcTmux $serverName @("-N", "list-sessions")
		if ($good.Out -notlike "*acl:*") {
			throw "client did not reconnect after endpoint restore"
		}
		Write-Host ("[IPC-ACL] iteration {0}/{1} passed" -f `
		    $i, $Iterations)
	} finally {
		try {
			Invoke-IpcTmux $serverName @("kill-server") `
			    -AllowFailure | Out-Null
		} catch {
		}
		if (Test-Path -LiteralPath $endpoint) {
			Remove-Item -LiteralPath $endpoint -Force
		}
	}
}

Write-Host ("Windows IPC ACL stress passed: iterations={0}" -f $Iterations)
