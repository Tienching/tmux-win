param(
	[string]$Tmux = "",
	[string]$Output = "",
	[int]$TimeoutSeconds = 30,
	[System.Management.Automation.PSCredential]$OtherUserCredential = $null,
	[switch]$CreateTemporaryLocalUser,
	[switch]$RunSystemTaskProbe,
	[switch]$RequireComplete
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

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "dist\ipc-boundary-audit.json"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}
$Output = [System.IO.Path]::GetFullPath($Output)

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

function Invoke-BoundaryTmux([string]$ServerName, [string[]]$Arguments,
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
	$stdoutTask = $process.StandardOutput.ReadToEndAsync()
	$stderrTask = $process.StandardError.ReadToEndAsync()
	if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
		try {
			$process.Kill()
		} catch {
		}
		throw "tmux timed out: $($Arguments -join ' ')"
	}
	$process.WaitForExit()
	$result = [pscustomobject]@{
		ExitCode = $process.ExitCode
		Out = $stdoutTask.Result
		Err = $stderrTask.Result
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

function Add-Check([System.Collections.Generic.List[object]]$List,
    [string]$Name, [string]$Status, [string]$Detail) {
	$List.Add([pscustomobject]@{
	    Name = $Name
	    Status = $Status
	    Detail = $Detail
	})
}

function Get-SidValue($IdentityReference) {
	try {
		if ($IdentityReference -is [string]) {
			$account =
			    [System.Security.Principal.NTAccount]$IdentityReference
			return $account.Translate(
			    [System.Security.Principal.SecurityIdentifier]).Value
		}
		return $IdentityReference.Translate(
		    [System.Security.Principal.SecurityIdentifier]).Value
	} catch {
		return [string]$IdentityReference
	}
}

function Test-IsAdministrator {
	$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
	$principal =
	    [System.Security.Principal.WindowsPrincipal]::new($identity)
	return $principal.IsInRole(
	    [System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function New-TemporaryPassword {
	return ("T!" + [Guid]::NewGuid().ToString("N").Substring(0, 20) +
	    "aA1!")
}

function Invoke-NetUser([string[]]$Arguments) {
	$output = & net.exe user @Arguments 2>&1
	if ($LASTEXITCODE -ne 0) {
		throw "net user failed: $($Arguments -join ' '): $($output -join ' ')"
	}
}

function New-TemporaryLocalUser([string]$Name, [string]$Password,
    [System.Security.SecureString]$SecurePassword) {
	$cmd = Get-Command New-LocalUser -ErrorAction SilentlyContinue
	if ($cmd -ne $null) {
		New-LocalUser -Name $Name -Password $SecurePassword `
		    -AccountNeverExpires -PasswordNeverExpires `
		    -UserMayNotChangePassword `
		    -Description "Temporary tmux Windows IPC boundary probe" |
		    Out-Null
		return
	}
	Invoke-NetUser @($Name, $Password, "/add", "/expires:never")
}

function Remove-TemporaryLocalUser([string]$Name) {
	$cmd = Get-Command Remove-LocalUser -ErrorAction SilentlyContinue
	if ($cmd -ne $null) {
		Remove-LocalUser -Name $Name -ErrorAction Stop
		return
	}
	Invoke-NetUser @($Name, "/delete")
}

function Assert-EndpointFormat([string]$Path) {
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
	return [pscustomobject]@{
		Port = [int]$lines[1]
		TokenLength = $lines[2].Length
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
			$inner = $_.Exception.InnerException
			if ($inner -is [System.Net.Sockets.SocketException] -and
			    $inner.SocketErrorCode -eq
			    [System.Net.Sockets.SocketError]::TimedOut) {
				throw "server kept connection open after bad token"
			}
		}
	} finally {
		$client.Close()
	}
}

function Initialize-LogonUser {
	if ("TmuxWin32LogonUser" -as [type]) {
		return
	}
	Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class TmuxWin32LogonUser
{
	[DllImport("advapi32.dll", SetLastError = true,
	    CharSet = CharSet.Unicode)]
	public static extern bool LogonUserW(string username, string domain,
	    string password, int logonType, int logonProvider,
	    out IntPtr token);

	[DllImport("kernel32.dll", SetLastError = true)]
	public static extern bool CloseHandle(IntPtr handle);
}
"@
}

function Split-CredentialUserName([string]$UserName) {
	if ($UserName.Contains("\")) {
		$parts = $UserName.Split([char]'\', 2)
		return [pscustomobject]@{
			Name = $parts[1]
			Domain = $parts[0]
		}
	}
	if ($UserName.Contains("@")) {
		return [pscustomobject]@{
			Name = $UserName
			Domain = $null
		}
	}
	return [pscustomobject]@{
		Name = $UserName
		Domain = "."
	}
}

function Test-OtherUserEndpointReadByImpersonation([string]$Path,
    [System.Management.Automation.PSCredential]$Credential) {
	Initialize-LogonUser
	$parts = Split-CredentialUserName $Credential.UserName
	$password = ConvertFrom-SecureStringToPlainText $Credential.Password
	$token = [IntPtr]::Zero
	$identity = $null
	$context = $null
	try {
		$ok = [TmuxWin32LogonUser]::LogonUserW($parts.Name,
		    $parts.Domain, $password, 3, 0, [ref]$token)
		if (-not $ok) {
			return "logon-failed:$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
		}

		$identity = [System.Security.Principal.WindowsIdentity]::new(
		    $token)
		$context = $identity.Impersonate()
		try {
			Get-Content -LiteralPath $Path -Raw -ErrorAction Stop |
			    Out-Null
			return "readable"
		} catch [System.UnauthorizedAccessException] {
			return "denied"
		} catch {
			return "unexpected:$($_.Exception.GetType().FullName):$($_.Exception.Message)"
		}
	} finally {
		if ($context -ne $null) {
			$context.Undo()
			$context.Dispose()
		}
		if ($identity -ne $null) {
			$identity.Dispose()
		}
		if ($token -ne [IntPtr]::Zero) {
			[void][TmuxWin32LogonUser]::CloseHandle($token)
		}
		$password = $null
	}
}

function Test-OtherUserEndpointRead([string]$Path,
    [System.Management.Automation.PSCredential]$Credential) {
	$impersonationResult = Test-OtherUserEndpointReadByImpersonation `
	    $Path $Credential
	if ($impersonationResult -eq "denied") {
		return 0
	}
	if ($impersonationResult -eq "readable") {
		return 10
	}

	$command = @"
try {
	Get-Content -LiteralPath '$($Path.Replace("'", "''"))' -Raw -ErrorAction Stop | Out-Null
	exit 10
} catch [System.UnauthorizedAccessException] {
	exit 0
} catch {
	exit 11
}
"@
	$encoded = [Convert]::ToBase64String(
	    [System.Text.Encoding]::Unicode.GetBytes($command))
	$psiArguments = "-NoProfile -EncodedCommand $encoded"
	try {
		$process = Start-Process -FilePath `
		    (Get-Command powershell.exe).Source `
		    -ArgumentList $psiArguments -Credential $Credential `
		    -PassThru -Wait -WindowStyle Hidden
		return $process.ExitCode
	} catch {
		$taskResult = Test-OtherUserEndpointReadViaTask $Path `
		    $Credential
		if ($taskResult -eq "denied") {
			return 0
		}
		if ($taskResult -eq "readable") {
			return 10
		}
		return 11
	}
}

function Test-SystemTaskEndpointRead([string]$Path) {
	$taskName = "tmux-ipc-boundary-" +
	    [Guid]::NewGuid().ToString("N")
	$probeDir = Join-Path $env:ProgramData $taskName
	$resultPath = Join-Path $probeDir "result.txt"
	$probeScript = Join-Path $probeDir "probe.ps1"
	New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
	try {
		$command = @"
try {
	Get-Content -LiteralPath '$($Path.Replace("'", "''"))' -Raw -ErrorAction Stop | Out-Null
	'readable' | Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
} catch [System.UnauthorizedAccessException] {
	'denied' | Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
} catch {
	('unexpected:' + `$_.Exception.GetType().FullName + ':' + `$_.Exception.Message) |
	    Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
}
"@
		Set-Content -LiteralPath $probeScript -Value $command `
		    -Encoding ascii
		$powershell = (Get-Command powershell.exe).Source
		$taskCommand = "`"$powershell`" -NoProfile " +
		    "-ExecutionPolicy Bypass -File `"$probeScript`""
		$runAt = (Get-Date).AddMinutes(1).ToString("HH:mm")

		$createOutput = & schtasks.exe /Create /TN $taskName /SC ONCE `
		    /ST $runAt /RU SYSTEM /RL HIGHEST /TR $taskCommand /F 2>&1
		if ($LASTEXITCODE -ne 0) {
			return "schtasks-create-failed:$($createOutput -join ' ')"
		}
		$runOutput = & schtasks.exe /Run /TN $taskName 2>&1
		if ($LASTEXITCODE -ne 0) {
			return "schtasks-run-failed:$($runOutput -join ' ')"
		}
		$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
		while ([DateTime]::UtcNow -lt $deadline) {
			if (Test-Path -LiteralPath $resultPath) {
				return (Get-Content -LiteralPath $resultPath -Raw).
				    Trim()
			}
			Start-Sleep -Milliseconds 500
		}
		return "timeout"
	} finally {
		try {
			& schtasks.exe /Delete /TN $taskName /F | Out-Null
		} catch {
		}
		if (Test-Path -LiteralPath $probeDir) {
			Remove-Item -LiteralPath $probeDir -Recurse -Force
		}
	}
}

function ConvertFrom-SecureStringToPlainText(
    [System.Security.SecureString]$SecureString) {
	$bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR(
	    $SecureString)
	try {
		return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
	} finally {
		[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
	}
}

function Test-OtherUserEndpointReadViaTask([string]$Path,
    [System.Management.Automation.PSCredential]$Credential) {
	$taskName = "tmux-ipc-other-" +
	    [Guid]::NewGuid().ToString("N")
	$publicRoot = Join-Path $env:PUBLIC "Documents"
	if (-not (Test-Path -LiteralPath $publicRoot)) {
		$publicRoot = $env:ProgramData
	}
	$probeDir = Join-Path $publicRoot $taskName
	$resultPath = Join-Path $probeDir "result.txt"
	$probeScript = Join-Path $probeDir "probe.ps1"
	New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
	try {
		$grantOutput = & icacls.exe $probeDir /grant `
		    "$($Credential.UserName):(OI)(CI)M" 2>&1
		if ($LASTEXITCODE -ne 0) {
			return "icacls-failed:$($grantOutput -join ' ')"
		}
		$command = @"
try {
	Get-Content -LiteralPath '$($Path.Replace("'", "''"))' -Raw -ErrorAction Stop | Out-Null
	'readable' | Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
} catch [System.UnauthorizedAccessException] {
	'denied' | Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
} catch {
	('unexpected:' + `$_.Exception.GetType().FullName + ':' + `$_.Exception.Message) |
	    Set-Content -LiteralPath '$($resultPath.Replace("'", "''"))' -Encoding ascii
}
"@
		Set-Content -LiteralPath $probeScript -Value $command `
		    -Encoding ascii
		$powershell = (Get-Command powershell.exe).Source
		$taskCommand = "`"$powershell`" -NoProfile " +
		    "-ExecutionPolicy Bypass -File `"$probeScript`""
		$runAt = (Get-Date).AddMinutes(1).ToString("HH:mm")
		$password = ConvertFrom-SecureStringToPlainText `
		    $Credential.Password
		$createOutput = & schtasks.exe /Create /TN $taskName /SC ONCE `
		    /ST $runAt /RU $Credential.UserName /RP $password `
		    /TR $taskCommand /F 2>&1
		if ($LASTEXITCODE -ne 0) {
			return "schtasks-create-failed:$($createOutput -join ' ')"
		}
		$runOutput = & schtasks.exe /Run /TN $taskName 2>&1
		if ($LASTEXITCODE -ne 0) {
			return "schtasks-run-failed:$($runOutput -join ' ')"
		}
		$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
		while ([DateTime]::UtcNow -lt $deadline) {
			if (Test-Path -LiteralPath $resultPath) {
				return (Get-Content -LiteralPath $resultPath -Raw).
				    Trim()
			}
			Start-Sleep -Milliseconds 500
		}
		return "timeout"
	} finally {
		try {
			& schtasks.exe /Delete /TN $taskName /F | Out-Null
		} catch {
		}
		if (Test-Path -LiteralPath $probeDir) {
			Remove-Item -LiteralPath $probeDir -Recurse -Force
		}
	}
}

$checks = [System.Collections.Generic.List[object]]::new()
$serverName = "ipc-boundary-" + [Guid]::NewGuid().ToString("N")
$endpoint = Join-Path (Join-Path $env:LOCALAPPDATA "tmux") `
    ($serverName + ".endpoint")
$current = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$currentSid = $current.User.Value
$isAdministrator = Test-IsAdministrator
$temporaryUserName = ""
$temporaryUserCreated = $false

if ($CreateTemporaryLocalUser -and $null -ne $OtherUserCredential) {
	throw "-CreateTemporaryLocalUser cannot be combined with -OtherUserCredential"
}
if ($CreateTemporaryLocalUser) {
	if (-not $isAdministrator) {
		throw "-CreateTemporaryLocalUser requires an elevated PowerShell"
	}
	$temporaryUserName = "tmuxprobe" +
	    [Guid]::NewGuid().ToString("N").Substring(0, 8)
	$temporaryPassword = New-TemporaryPassword
	$securePassword = ConvertTo-SecureString $temporaryPassword `
	    -AsPlainText -Force
	New-TemporaryLocalUser $temporaryUserName $temporaryPassword `
	    $securePassword
	$temporaryUserCreated = $true
	$OtherUserCredential =
	    [System.Management.Automation.PSCredential]::new(
	    "$env:COMPUTERNAME\$temporaryUserName", $securePassword)
}

try {
	Invoke-BoundaryTmux $serverName @(
	    "new-session", "-d", "-s", "boundary", "cmd.exe") | Out-Null
	if (-not (Test-Path -LiteralPath $endpoint)) {
		throw "endpoint not created: $endpoint"
	}

	$acl = Get-Acl -LiteralPath $endpoint
	$ownerSid = Get-SidValue $acl.Owner
	if ($ownerSid -eq $currentSid) {
		Add-Check $checks "endpoint owner" "passed" $acl.Owner
	} elseif ($isAdministrator -and $ownerSid -eq "S-1-5-32-544") {
		Add-Check $checks "endpoint owner" "passed" `
		    "$($acl.Owner) owner under elevated process; DACL is checked separately"
	} else {
		Add-Check $checks "endpoint owner" "failed" `
		    "expected $currentSid got $ownerSid ($($acl.Owner))"
	}

	$rules = @($acl.Access)
	$currentAllows = @($rules | Where-Object {
	    $_.AccessControlType -eq "Allow" -and
	    (Get-SidValue $_.IdentityReference) -eq $currentSid
	})
	$fullControl = $false
	foreach ($rule in $currentAllows) {
		if (($rule.FileSystemRights -band
		    [System.Security.AccessControl.FileSystemRights]::FullControl) `
		    -eq [System.Security.AccessControl.FileSystemRights]::FullControl) {
			$fullControl = $true
		}
	}
	if ($fullControl) {
		Add-Check $checks "current user FullControl" "passed" `
		    $current.Name
	} else {
		Add-Check $checks "current user FullControl" "failed" `
		    "no FullControl allow ACE for $($current.Name)"
	}

	$broadSids = @{
	    "S-1-1-0" = "Everyone"
	    "S-1-5-11" = "Authenticated Users"
	    "S-1-5-32-545" = "Builtin Users"
	    "S-1-5-4" = "Interactive"
	    "S-1-5-2" = "Network"
	    "S-1-5-6" = "Service"
	    "S-1-5-19" = "LocalService"
	    "S-1-5-20" = "NetworkService"
	}
	$broadAllows = @($rules | Where-Object {
	    $_.AccessControlType -eq "Allow" -and
	    $broadSids.ContainsKey((Get-SidValue $_.IdentityReference))
	})
	if ($broadAllows.Count -eq 0) {
		Add-Check $checks "no broad endpoint allow ACEs" "passed" `
		    "checked $($broadSids.Values -join ', ')"
	} else {
		Add-Check $checks "no broad endpoint allow ACEs" "failed" `
		    ($broadAllows | ForEach-Object {
			"$($_.IdentityReference):$($_.FileSystemRights)"
		    }) -join "; "
	}

	$inheritedAllows = @($rules | Where-Object {
	    $_.AccessControlType -eq "Allow" -and $_.IsInherited
	})
	if ($inheritedAllows.Count -eq 0) {
		Add-Check $checks "no inherited allow ACEs" "passed" `
		    "endpoint has only explicit allow rules"
	} else {
		Add-Check $checks "no inherited allow ACEs" "failed" `
		    ($inheritedAllows | ForEach-Object {
			"$($_.IdentityReference):$($_.FileSystemRights)"
		    }) -join "; "
	}

	$format = Assert-EndpointFormat $endpoint
	Add-Check $checks "endpoint format" "passed" `
	    "port=$($format.Port);token_length=$($format.TokenLength)"

	Assert-BadTokenRejected $endpoint
	Add-Check $checks "bad token rejection" "passed" `
	    "raw loopback bad token was closed"

	$good = Invoke-BoundaryTmux $serverName @("-N", "list-sessions")
	if ($good.Out -like "*boundary:*") {
		Add-Check $checks "valid reconnect after bad token" "passed" `
		    "list-sessions succeeded"
	} else {
		Add-Check $checks "valid reconnect after bad token" "failed" `
		    "list-sessions output did not include boundary session"
	}

	if ($null -ne $OtherUserCredential) {
		try {
			$exitCode = Test-OtherUserEndpointRead $endpoint `
			    $OtherUserCredential
			if ($exitCode -eq 0) {
				Add-Check $checks "other-user endpoint read" `
				    "passed" `
				    "alternate credential could not read endpoint"
			} elseif ($exitCode -eq 10) {
				Add-Check $checks "other-user endpoint read" `
				    "failed" `
				    "alternate credential read endpoint token"
			} else {
				Add-Check $checks "other-user endpoint read" `
				    "blocked" `
				    "alternate process exited $exitCode"
			}
		} catch {
			Add-Check $checks "other-user endpoint read" "blocked" `
			    $_.Exception.Message
		}
	} else {
		Add-Check $checks "other-user endpoint read" "blocked" `
		    "no -OtherUserCredential supplied; alternatively pass -CreateTemporaryLocalUser from an elevated PowerShell"
	}

	if ($RunSystemTaskProbe) {
		if (-not $isAdministrator) {
			Add-Check $checks "SYSTEM scheduled-task endpoint read" `
			    "blocked" "requires an elevated PowerShell"
		} else {
			$systemResult = Test-SystemTaskEndpointRead $endpoint
			if ($systemResult -eq "denied") {
				Add-Check $checks `
				    "SYSTEM scheduled-task endpoint read" `
				    "passed" "SYSTEM probe could not read endpoint"
			} elseif ($systemResult -eq "readable") {
				Add-Check $checks `
				    "SYSTEM scheduled-task endpoint read" `
				    "failed" "SYSTEM probe read endpoint token"
			} else {
				Add-Check $checks `
				    "SYSTEM scheduled-task endpoint read" `
				    "blocked" $systemResult
			}
		}
	} else {
		Add-Check $checks "SYSTEM scheduled-task endpoint read" `
		    "blocked" "not requested; pass -RunSystemTaskProbe"
	}
} finally {
	try {
		Invoke-BoundaryTmux $serverName @("kill-server") `
		    -AllowFailure | Out-Null
	} catch {
	}
	if (Test-Path -LiteralPath $endpoint) {
		Remove-Item -LiteralPath $endpoint -Force
	}
	if ($temporaryUserCreated) {
		try {
			Remove-TemporaryLocalUser $temporaryUserName
			Add-Check $checks "temporary local user cleanup" `
			    "passed" $temporaryUserName
		} catch {
			Add-Check $checks "temporary local user cleanup" `
			    "failed" $_.Exception.Message
		}
	}
}

$failed = @($checks | Where-Object { $_.Status -eq "failed" })
$blocked = @($checks | Where-Object { $_.Status -eq "blocked" })
$status = if ($failed.Count -gt 0) {
	"failed"
} elseif ($blocked.Count -gt 0) {
	"partial"
} else {
	"passed"
}

$summary = [pscustomobject]@{
	GeneratedUtc = [DateTime]::UtcNow.ToString("o")
	Tmux = $Tmux
	ServerName = $serverName
	Endpoint = $endpoint
	CurrentUser = $current.Name
	CurrentSid = $currentSid
	IsAdministrator = [bool]$isAdministrator
	Status = $status
	Checks = @($checks.ToArray())
}

$outputDirectory = Split-Path -Parent $Output
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
	New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$summary | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $Output -Encoding ascii

Write-Host "ipc_boundary_audit=$Output"
Write-Host "status=$status"
Write-Host ("failed={0};blocked={1}" -f $failed.Count, $blocked.Count)
if ($RequireComplete -and $status -ne "passed") {
	exit 1
}
