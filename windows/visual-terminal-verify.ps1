param(
	[string]$Tmux = "",
	[string]$ResultPath = "",
	[string]$ScreenshotPath = "",
	[string]$Marker = "",
	[int]$Width = 100,
	[int]$Height = 30,
	[int]$HoldSeconds = 8,
	[int]$PollSeconds = 10,
	[switch]$DirectNoArgs
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

if ([string]::IsNullOrWhiteSpace($ResultPath)) {
	$ResultPath = Join-Path $Root "dist\visual-terminal-verify-latest.txt"
} elseif (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
	$ResultPath = Join-Path (Get-Location) $ResultPath
}
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)

if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
	if (-not [System.IO.Path]::IsPathRooted($ScreenshotPath)) {
		$ScreenshotPath = Join-Path (Get-Location) $ScreenshotPath
	}
	$ScreenshotPath = [System.IO.Path]::GetFullPath($ScreenshotPath)
}

if ([string]::IsNullOrWhiteSpace($Marker)) {
	$Marker = "TMUX_VISUAL_OK_" +
	    [Guid]::NewGuid().ToString("N").Substring(0, 8)
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

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

function Get-TerminalWindow([string]$Title) {
	$root = [System.Windows.Automation.AutomationElement]::RootElement
	$children = $root.FindAll(
	    [System.Windows.Automation.TreeScope]::Children,
	    [System.Windows.Automation.Condition]::TrueCondition)
	foreach ($child in $children) {
		try {
			$name = $child.Current.Name
			if ($name -eq $Title -or $name -like "*$Title*") {
				return $child
			}
		} catch {
		}
	}
	return $null
}

function Get-TerminalText([string]$Title) {
	$window = Get-TerminalWindow $Title
	if ($null -eq $window) {
		return ""
	}

	$text = [System.Collections.Generic.List[string]]::new()
	$elements = $window.FindAll(
	    [System.Windows.Automation.TreeScope]::Descendants,
	    [System.Windows.Automation.Condition]::TrueCondition)
	foreach ($element in $elements) {
		try {
			$pattern = $null
			if ($element.TryGetCurrentPattern(
			    [System.Windows.Automation.TextPattern]::Pattern,
			    [ref]$pattern)) {
				$value = $pattern.DocumentRange.GetText(-1)
				if (-not [string]::IsNullOrWhiteSpace($value)) {
					$text.Add($value)
				}
			}
		} catch {
		}
		try {
			$pattern = $null
			if ($element.TryGetCurrentPattern(
			    [System.Windows.Automation.ValuePattern]::Pattern,
			    [ref]$pattern)) {
				$value = $pattern.Current.Value
				if (-not [string]::IsNullOrWhiteSpace($value)) {
					$text.Add($value)
				}
			}
		} catch {
		}
	}
	return ($text -join "`n")
}

function Initialize-ScreenshotCapture {
	Add-Type -AssemblyName System.Drawing
	if ("TmuxVisualWindowCapture" -as [type]) {
		return
	}
	Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class TmuxVisualWindowCapture
{
	[StructLayout(LayoutKind.Sequential)]
	public struct RECT
	{
		public int Left;
		public int Top;
		public int Right;
		public int Bottom;
	}

	[DllImport("user32.dll", SetLastError = true)]
	public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
	[DllImport("user32.dll", SetLastError = true)]
	public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt,
	    uint nFlags);
}
"@
}

function Save-TerminalScreenshot([string]$Title, [string]$Path) {
	Initialize-ScreenshotCapture
	$window = Get-TerminalWindow $Title
	if ($null -eq $window) {
		throw "terminal window not found for screenshot: $Title"
	}

	$hwndValue = $window.Current.NativeWindowHandle
	if ($hwndValue -eq 0) {
		throw "terminal window has no native handle"
	}
	$hwnd = [IntPtr]$hwndValue

	$rect = New-Object TmuxVisualWindowCapture+RECT
	if (-not [TmuxVisualWindowCapture]::GetWindowRect($hwnd, [ref]$rect)) {
		throw "GetWindowRect failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
	}

	$width = $rect.Right - $rect.Left
	$height = $rect.Bottom - $rect.Top
	if ($width -lt 20 -or $height -lt 20) {
		throw "invalid terminal screenshot bounds: ${width}x${height}"
	}

	$directory = Split-Path -Parent $Path
	if (-not [string]::IsNullOrWhiteSpace($directory)) {
		New-Item -ItemType Directory -Force -Path $directory | Out-Null
	}

	$bitmap = [System.Drawing.Bitmap]::new($width, $height)
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	$hdc = $graphics.GetHdc()
	try {
		$printed = [TmuxVisualWindowCapture]::PrintWindow($hwnd, $hdc, 2)
	} finally {
		$graphics.ReleaseHdc($hdc)
		$graphics.Dispose()
	}
	if (-not $printed) {
		$bitmap.Dispose()
		throw "PrintWindow failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
	}

	try {
		$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
	} finally {
		$bitmap.Dispose()
	}

	return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).
	    Hash.ToLowerInvariant()
}

$Title = "tmux visual verification"
$ChildResultPath = Join-Path ([System.IO.Path]::GetTempPath()) (
    "tmux-visual-child-" + [Guid]::NewGuid().ToString("N") + ".txt")
$HostScript = Join-Path $PSScriptRoot "visual-console-verify.ps1"
$arguments = @(
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    $HostScript,
    "-Tmux",
    $Tmux,
    "-ResultPath",
    $ChildResultPath,
    "-Marker",
    $Marker,
    "-Width",
    [string]$Width,
    "-Height",
    [string]$Height,
    "-HoldSeconds",
    [string]$HoldSeconds,
    "-NoAssert"
)
if ($DirectNoArgs) {
	$arguments += "-DirectNoArgs"
}

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = (Get-Command powershell.exe).Source
$psi.Arguments = ($arguments | ForEach-Object {
    ConvertTo-WindowsArgument $_
}) -join " "
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $false

$ok = $false
$uiaText = ""
$screenshotHash = ""
$screenshotError = ""
$child = [System.Diagnostics.Process]::Start($psi)
try {
	$deadline = [DateTime]::UtcNow.AddSeconds($PollSeconds)
	while ([DateTime]::UtcNow -lt $deadline) {
		Start-Sleep -Milliseconds 500
		$uiaText = Get-TerminalText $Title
		if ($uiaText -like "*$Marker*") {
			$ok = $true
			if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
				try {
					$screenshotHash = Save-TerminalScreenshot `
					    $Title $ScreenshotPath
				} catch {
					$screenshotError = $_.Exception.Message
					throw
				}
			}
			break
		}
		if ($child.HasExited) {
			break
		}
	}

	$waitMs = [Math]::Max(1000, ($HoldSeconds + 5) * 1000)
	if (-not $child.WaitForExit($waitMs)) {
		$child.Kill()
		$child.WaitForExit()
	}
} finally {
	$resultDirectory = Split-Path -Parent $ResultPath
	if (-not [string]::IsNullOrWhiteSpace($resultDirectory)) {
		New-Item -ItemType Directory -Force -Path $resultDirectory |
		    Out-Null
	}
	$childResult = ""
	if (Test-Path -LiteralPath $ChildResultPath) {
		$childResult = Get-Content -LiteralPath $ChildResultPath -Raw
		Remove-Item -LiteralPath $ChildResultPath -Force
	}
	@(
	    "ok=$ok",
	    "marker=$Marker",
	    "tmux=$Tmux",
	    "screenshot=$ScreenshotPath",
	    "screenshot_sha256=$screenshotHash",
	    "screenshot_error=$screenshotError",
	    "child_exit=$($child.ExitCode)",
	    "uia_text:",
	    $uiaText,
	    "child_result:",
	    $childResult
	) | Set-Content -LiteralPath $ResultPath -Encoding utf8
}

if (-not $ok) {
	throw "visible Windows Terminal did not show marker: $Marker"
}
Write-Host "visual_terminal_verify=passed"
Write-Host "result=$ResultPath"
if (-not [string]::IsNullOrWhiteSpace($ScreenshotPath)) {
	Write-Host "screenshot=$ScreenshotPath"
}
