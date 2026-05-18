param(
	[string]$Tmux = "",
	[string]$Output = "",
	[string]$Objdump = "",
	[string]$ZipPath = "",
	[int]$SmokeTimeoutSeconds = 60,
	[switch]$Clean,
	[switch]$RunSmoke,
	[switch]$Zip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($Tmux)) {
	$Tmux = Join-Path $Root "tmux.exe"
} elseif (-not [System.IO.Path]::IsPathRooted($Tmux)) {
	$Tmux = Join-Path (Get-Location) $Tmux
}
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path

if ([string]::IsNullOrWhiteSpace($Output)) {
	$Output = Join-Path $Root "dist\tmux-win32-portable"
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
	$Output = Join-Path (Get-Location) $Output
}

function Resolve-ToolPath([string]$Tool) {
	if ([string]::IsNullOrWhiteSpace($Tool)) {
		return ""
	}
	if ([System.IO.Path]::IsPathRooted($Tool) -and
	    (Test-Path -LiteralPath $Tool)) {
		return (Resolve-Path -LiteralPath $Tool).Path
	}
	$cmd = Get-Command $Tool -ErrorAction SilentlyContinue
	if ($cmd -ne $null -and -not [string]::IsNullOrWhiteSpace($cmd.Source)) {
		return $cmd.Source
	}
	return ""
}

function Find-Objdump {
	if (-not [string]::IsNullOrWhiteSpace($Objdump)) {
		$resolved = Resolve-ToolPath $Objdump
		if (-not [string]::IsNullOrWhiteSpace($resolved)) {
			return $resolved
		}
		throw "objdump not found: $Objdump"
	}

	foreach ($candidate in @(
	    "C:\msys64\mingw64\bin\objdump.exe",
	    "D:\msys64\mingw64\bin\objdump.exe",
	    "C:\msys64\ucrt64\bin\objdump.exe",
	    "D:\msys64\ucrt64\bin\objdump.exe",
	    "objdump")) {
		$resolved = Resolve-ToolPath $candidate
		if (-not [string]::IsNullOrWhiteSpace($resolved)) {
			return $resolved
		}
	}
	throw "objdump not found; install MSYS2 binutils or pass -Objdump"
}

$ObjdumpPath = Find-Objdump

$SystemDlls = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
foreach ($dll in @(
    "ADVAPI32.dll", "BCRYPT.dll", "COMDLG32.dll", "CRYPT32.dll",
    "GDI32.dll", "IMM32.dll", "IPHLPAPI.dll", "KERNEL32.dll", "msvcrt.dll",
    "NTDLL.dll", "OLE32.dll", "OLEAUT32.dll", "SHELL32.dll",
    "SHLWAPI.dll", "USER32.dll", "VERSION.dll", "WINMM.dll",
    "WINSPOOL.DRV", "WS2_32.dll")) {
	[void]$SystemDlls.Add($dll)
}

$SearchPaths = [System.Collections.Generic.List[string]]::new()

function Add-SearchPath([string]$Path) {
	if ([string]::IsNullOrWhiteSpace($Path) -or
	    -not (Test-Path -LiteralPath $Path)) {
		return
	}
	$resolved = (Resolve-Path -LiteralPath $Path).Path
	foreach ($existing in $SearchPaths) {
		if ($existing -ieq $resolved) {
			return
		}
	}
	$SearchPaths.Add($resolved)
}

Add-SearchPath (Split-Path -Parent $Tmux)
Add-SearchPath (Split-Path -Parent $ObjdumpPath)
foreach ($entry in ($env:PATH -split ";")) {
	Add-SearchPath $entry
}
foreach ($candidate in @(
    "C:\msys64\mingw64\bin", "D:\msys64\mingw64\bin",
    "C:\msys64\ucrt64\bin", "D:\msys64\ucrt64\bin",
    "C:\msys64\clang64\bin", "D:\msys64\clang64\bin")) {
	Add-SearchPath $candidate
}

function Get-ImportDllNames([string]$Binary) {
	$output = & $ObjdumpPath -p $Binary 2>&1
	if ($LASTEXITCODE -ne 0) {
		$output | Select-Object -First 80 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "objdump failed: $Binary"
	}
	$imports = [System.Collections.Generic.List[string]]::new()
	foreach ($line in $output) {
		if ($line -match "DLL Name:\s*(.+)$") {
			$imports.Add($Matches[1].Trim())
		}
	}
	return @($imports.ToArray())
}

function Test-SystemDll([string]$Name) {
	if ($SystemDlls.Contains($Name)) {
		return $true
	}
	if ($Name -like "api-ms-win-*" -or $Name -like "ext-ms-*") {
		return $true
	}
	return $false
}

function Find-Dll([string]$Name) {
	foreach ($directory in $SearchPaths) {
		$candidate = Join-Path $directory $Name
		if (Test-Path -LiteralPath $candidate) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}
	throw "dependency not found: $Name"
}

function Get-FileRecord([string]$Path, [string]$Source = "") {
	$item = Get-Item -LiteralPath $Path
	$hash = Get-FileHash -LiteralPath $Path -Algorithm SHA256
	return [pscustomobject]@{
		Name = $item.Name
		Path = $item.FullName
		Source = $Source
		Size = $item.Length
		SHA256 = $hash.Hash.ToLowerInvariant()
	}
}

function Assert-OutputNotRunning([string]$PackagePath) {
	$tmuxPath = [System.IO.Path]::GetFullPath(
	    (Join-Path $PackagePath "tmux.exe"))
	$running = @(Get-CimInstance Win32_Process -Filter "name = 'tmux.exe'" |
	    Where-Object {
		($_.ExecutablePath -ne $null -and
		    $_.ExecutablePath -ieq $tmuxPath) -or
		($_.CommandLine -ne $null -and
		    $_.CommandLine.IndexOf($tmuxPath,
		    [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
	    })
	if ($running.Count -eq 0) {
		return
	}

	$details = ($running | ForEach-Object {
	    "pid={0} command={1}" -f $_.ProcessId, $_.CommandLine
	}) -join "`n"
	throw @"
portable output is in use by running tmux.exe:
$details

Detach attached clients with Ctrl-b then d, then run:
  $tmuxPath kill-server
"@
}

$Output = [System.IO.Path]::GetFullPath($Output)
Assert-OutputNotRunning $Output
if ($Clean -and (Test-Path -LiteralPath $Output)) {
	$rootPrefix = $Root + [System.IO.Path]::DirectorySeparatorChar
	if (-not $Output.StartsWith($rootPrefix,
	    [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "refusing to clean outside workspace: $Output"
	}
	Remove-Item -LiteralPath $Output -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Output | Out-Null

if ([string]::IsNullOrWhiteSpace($ZipPath)) {
	$ZipPath = $Output.TrimEnd('\', '/') + ".zip"
} elseif (-not [System.IO.Path]::IsPathRooted($ZipPath)) {
	$ZipPath = Join-Path (Get-Location) $ZipPath
}
$ZipPath = [System.IO.Path]::GetFullPath($ZipPath)

$packagedTmux = Join-Path $Output "tmux.exe"
Copy-Item -LiteralPath $Tmux -Destination $packagedTmux -Force

$pending = [System.Collections.Generic.Queue[string]]::new()
$seenBinaries = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
$copiedDlls = [System.Collections.Generic.List[object]]::new()
$pending.Enqueue($Tmux)

while ($pending.Count -gt 0) {
	$binary = (Resolve-Path -LiteralPath $pending.Dequeue()).Path
	if (-not $seenBinaries.Add($binary)) {
		continue
	}
	foreach ($name in Get-ImportDllNames $binary) {
		if (Test-SystemDll $name) {
			continue
		}
		$dll = Find-Dll $name
		$target = Join-Path $Output (Split-Path -Leaf $dll)
		if (-not (Test-Path -LiteralPath $target)) {
			Copy-Item -LiteralPath $dll -Destination $target -Force
			$copiedDlls.Add([pscustomobject]@{
			    Name = Split-Path -Leaf $dll
			    Source = $dll
			})
		}
		Add-SearchPath (Split-Path -Parent $dll)
		$pending.Enqueue($dll)
	}
}

$oldPath = $env:PATH
try {
	$env:PATH = $Output + ";" + $oldPath
	$version = (& $packagedTmux -V 2>&1)
	if ($LASTEXITCODE -ne 0) {
		throw "packaged tmux.exe failed to start"
	}
	if ($RunSmoke) {
		$smoke = Join-Path $PSScriptRoot "smoke-runtime.ps1"
		& $smoke -Tmux $packagedTmux `
		    -TimeoutSeconds $SmokeTimeoutSeconds
		if ($LASTEXITCODE -ne 0) {
			throw "runtime smoke failed for packaged tmux.exe"
		}
	}
} finally {
	$env:PATH = $oldPath
}

$dependencyRecords = foreach ($dll in ($copiedDlls.ToArray() |
    Sort-Object Name)) {
	Get-FileRecord (Join-Path $Output $dll.Name) $dll.Source
}
$fileRecords = @(
    Get-FileRecord $packagedTmux $Tmux
) + @($dependencyRecords)

$manifest = [pscustomobject]@{
	Package = $Output
	Version = ($version -join "`n").Trim()
	Tmux = $packagedTmux
	Objdump = $ObjdumpPath
	Files = $fileRecords
	Dependencies = @($dependencyRecords)
}
$manifestPath = Join-Path $Output "manifest.json"
$manifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding ascii

if ($Zip) {
	$zipDirectory = Split-Path -Parent $ZipPath
	if (-not [string]::IsNullOrWhiteSpace($zipDirectory)) {
		New-Item -ItemType Directory -Force -Path $zipDirectory |
		    Out-Null
	}
	$outputPrefix = $Output.TrimEnd('\', '/') +
	    [System.IO.Path]::DirectorySeparatorChar
	if ($ZipPath.StartsWith($outputPrefix,
	    [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "refusing to create archive inside package directory: $ZipPath"
	}
	if (Test-Path -LiteralPath $ZipPath) {
		Remove-Item -LiteralPath $ZipPath -Force
	}
	Compress-Archive -Path (Join-Path $Output "*") `
	    -DestinationPath $ZipPath -Force
	$zipHash = Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256
	$zipHashPath = $ZipPath + ".sha256"
	("{0}  {1}" -f $zipHash.Hash.ToLowerInvariant(),
	    (Split-Path -Leaf $ZipPath)) |
	    Set-Content -LiteralPath $zipHashPath -Encoding ascii
}

Write-Host "package=$Output"
Write-Host "version=$($manifest.Version)"
Write-Host "dependencies=$($copiedDlls.Count)"
Write-Host "manifest=$manifestPath"
if ($Zip) {
	Write-Host "zip=$ZipPath"
	Write-Host "zip_sha256=$zipHashPath"
}
