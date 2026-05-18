param(
	[string]$Tmux = "",
	[string]$ResultPath = "",
	[string]$Marker = "",
	[int]$Width = 100,
	[int]$Height = 30,
	[int]$HoldSeconds = 8,
	[switch]$DirectNoArgs,
	[switch]$NoAssert
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
	$ResultPath = Join-Path $Root "dist\visual-console-verify-latest.txt"
} elseif (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
	$ResultPath = Join-Path (Get-Location) $ResultPath
}
$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)

$ServerName = "visual-" + [Guid]::NewGuid().ToString("N")
if ([string]::IsNullOrWhiteSpace($Marker)) {
	$Marker = "TMUX_VISUAL_OK_" +
	    [Guid]::NewGuid().ToString("N").Substring(0, 8)
}
$TempDirectory = Join-Path ([System.IO.Path]::GetTempPath()) (
    "tmux-visual-" + [Guid]::NewGuid().ToString("N"))
$MarkerScript = Join-Path $TempDirectory "visual-marker.cmd"

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class TmuxVisualConsole
{
	const int STD_INPUT_HANDLE = -10;
	const int STD_OUTPUT_HANDLE = -11;
	const int STD_ERROR_HANDLE = -12;
	const uint GENERIC_READ = 0x80000000;
	const uint GENERIC_WRITE = 0x40000000;
	const uint FILE_SHARE_READ = 0x00000001;
	const uint FILE_SHARE_WRITE = 0x00000002;
	const uint OPEN_EXISTING = 3;
	const uint HANDLE_FLAG_INHERIT = 0x00000001;
	const ushort KEY_EVENT = 0x0001;
	const uint SHIFT_PRESSED = 0x0010;
	const uint LEFT_CTRL_PRESSED = 0x0008;
	const uint LEFT_ALT_PRESSED = 0x0002;
	static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

	[StructLayout(LayoutKind.Sequential)]
	public struct COORD
	{
		public short X;
		public short Y;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct SMALL_RECT
	{
		public short Left;
		public short Top;
		public short Right;
		public short Bottom;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct CONSOLE_SCREEN_BUFFER_INFO
	{
		public COORD dwSize;
		public COORD dwCursorPosition;
		public short wAttributes;
		public SMALL_RECT srWindow;
		public COORD dwMaximumWindowSize;
	}

	[StructLayout(LayoutKind.Sequential)]
	public struct KEY_EVENT_RECORD
	{
		[MarshalAs(UnmanagedType.Bool)]
		public bool bKeyDown;
		public ushort wRepeatCount;
		public ushort wVirtualKeyCode;
		public ushort wVirtualScanCode;
		public char UnicodeChar;
		public uint dwControlKeyState;
	}

	[StructLayout(LayoutKind.Explicit)]
	public struct INPUT_RECORD
	{
		[FieldOffset(0)]
		public ushort EventType;
		[FieldOffset(4)]
		public KEY_EVENT_RECORD KeyEvent;
	}

	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool AllocConsole();
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool FreeConsole();
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern IntPtr CreateFileW(string fileName, uint desiredAccess,
	    uint shareMode, IntPtr securityAttributes, uint creationDisposition,
	    uint flagsAndAttributes, IntPtr templateFile);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool SetStdHandle(int nStdHandle, IntPtr hHandle);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool SetHandleInformation(IntPtr hObject, uint dwMask,
	    uint dwFlags);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr GetStdHandle(int nStdHandle);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool SetConsoleScreenBufferSize(IntPtr hConsoleOutput,
	    COORD dwSize);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool SetConsoleWindowInfo(IntPtr hConsoleOutput,
	    bool bAbsolute, ref SMALL_RECT lpConsoleWindow);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GetConsoleScreenBufferInfo(IntPtr hConsoleOutput,
	    out CONSOLE_SCREEN_BUFFER_INFO lpConsoleScreenBufferInfo);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool ReadConsoleOutputCharacterW(IntPtr hConsoleOutput,
	    StringBuilder lpCharacter, uint nLength, COORD dwReadCoord,
	    out uint lpNumberOfCharsRead);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool CloseHandle(IntPtr hObject);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool WriteConsoleInputW(IntPtr hConsoleInput,
	    INPUT_RECORD[] lpBuffer, uint nLength,
	    out uint lpNumberOfEventsWritten);
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern bool SetConsoleTitleW(string title);
	[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern short VkKeyScanW(char ch);
	[DllImport("user32.dll", SetLastError = true)]
	static extern uint MapVirtualKeyW(uint uCode, uint uMapType);

	public static void Open(string title, int width, int height)
	{
		FreeConsole();
		if (!AllocConsole())
			throw new Win32Exception(Marshal.GetLastWin32Error());
		SetConsoleTitleW(title);

		IntPtr input = CreateFileW("CONIN$", GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero,
		    OPEN_EXISTING, 0, IntPtr.Zero);
		if (input == INVALID_HANDLE_VALUE)
			throw new Win32Exception(Marshal.GetLastWin32Error());
		IntPtr output = CreateFileW("CONOUT$", GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero,
		    OPEN_EXISTING, 0, IntPtr.Zero);
		if (output == INVALID_HANDLE_VALUE)
			throw new Win32Exception(Marshal.GetLastWin32Error());
		if (!SetHandleInformation(input, HANDLE_FLAG_INHERIT,
		    HANDLE_FLAG_INHERIT))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		if (!SetHandleInformation(output, HANDLE_FLAG_INHERIT,
		    HANDLE_FLAG_INHERIT))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		SetStdHandle(STD_INPUT_HANDLE, input);
		SetStdHandle(STD_OUTPUT_HANDLE, output);
		SetStdHandle(STD_ERROR_HANDLE, output);
		SetSize(output, width, height);
	}

	static void SetSize(IntPtr output, int width, int height)
	{
		CONSOLE_SCREEN_BUFFER_INFO before;
		if (!GetConsoleScreenBufferInfo(output, out before))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		int currentWidth = before.srWindow.Right -
		    before.srWindow.Left + 1;
		int currentHeight = before.srWindow.Bottom -
		    before.srWindow.Top + 1;
		if (width < currentWidth || height < currentHeight)
			SetWindow(output, width, height);
		COORD size = new COORD();
		size.X = (short)width;
		size.Y = (short)Math.Max(height, 100);
		if (!SetConsoleScreenBufferSize(output, size))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		SetWindow(output, width, height);
	}

	static void SetWindow(IntPtr output, int width, int height)
	{
		SMALL_RECT rect = new SMALL_RECT();
		rect.Left = 0;
		rect.Top = 0;
		rect.Right = (short)(width - 1);
		rect.Bottom = (short)(height - 1);
		if (!SetConsoleWindowInfo(output, true, ref rect))
			throw new Win32Exception(Marshal.GetLastWin32Error());
	}

	public static void SendText(string text)
	{
		IntPtr input = GetStdHandle(STD_INPUT_HANDLE);
		if (input == INVALID_HANDLE_VALUE || input == IntPtr.Zero)
			throw new Win32Exception(Marshal.GetLastWin32Error());
		INPUT_RECORD[] records = new INPUT_RECORD[text.Length * 2];
		int offset = 0;
		foreach (char ch in text) {
			ushort vk, scan;
			uint control;
			KeyInfo(ch, out vk, out scan, out control);
			records[offset++] = KeyRecord(ch, vk, scan, control, true);
			records[offset++] = KeyRecord(ch, vk, scan, control, false);
		}
		uint written;
		if (!WriteConsoleInputW(input, records, (uint)records.Length,
		    out written))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		if (written != records.Length)
			throw new InvalidOperationException("short input write");
	}

	public static string ReadVisibleText()
	{
		IntPtr output = CreateFileW("CONOUT$", GENERIC_READ | GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero,
		    OPEN_EXISTING, 0, IntPtr.Zero);
		if (output == INVALID_HANDLE_VALUE || output == IntPtr.Zero)
			throw new Win32Exception(Marshal.GetLastWin32Error());
		try {
			CONSOLE_SCREEN_BUFFER_INFO info;
			if (!GetConsoleScreenBufferInfo(output, out info))
				throw new Win32Exception(Marshal.GetLastWin32Error());
			int width = info.srWindow.Right - info.srWindow.Left + 1;
			StringBuilder result = new StringBuilder();
			for (short y = info.srWindow.Top; y <= info.srWindow.Bottom;
			    y++) {
				StringBuilder line = new StringBuilder(
				    new string(' ', width));
				COORD coord = new COORD();
				coord.X = info.srWindow.Left;
				coord.Y = y;
				uint read;
				if (!ReadConsoleOutputCharacterW(output, line,
				    (uint)width, coord, out read))
					throw new Win32Exception(
					    Marshal.GetLastWin32Error());
				int count = Math.Min((int)read, line.Length);
				result.AppendLine(line.ToString(0, count).TrimEnd());
			}
			return result.ToString();
		} finally {
			CloseHandle(output);
		}
	}

	static void KeyInfo(char ch, out ushort vk, out ushort scan,
	    out uint control)
	{
		control = 0;
		if (ch == '\r' || ch == '\n') {
			vk = 13;
			scan = 28;
			return;
		}
		short key = VkKeyScanW(ch);
		if (key == -1) {
			vk = 0;
			scan = 0;
			return;
		}
		vk = (ushort)(key & 0xff);
		byte state = (byte)((key >> 8) & 0xff);
		if ((state & 1) != 0)
			control |= SHIFT_PRESSED;
		if ((state & 2) != 0)
			control |= LEFT_CTRL_PRESSED;
		if ((state & 4) != 0)
			control |= LEFT_ALT_PRESSED;
		scan = (ushort)MapVirtualKeyW(vk, 0);
	}

	static INPUT_RECORD KeyRecord(char ch, ushort vk, ushort scan,
	    uint control, bool down)
	{
		INPUT_RECORD record = new INPUT_RECORD();
		record.EventType = KEY_EVENT;
		record.KeyEvent.bKeyDown = down;
		record.KeyEvent.wRepeatCount = 1;
		record.KeyEvent.wVirtualKeyCode = vk;
		record.KeyEvent.wVirtualScanCode = scan;
		record.KeyEvent.UnicodeChar = ch;
		record.KeyEvent.dwControlKeyState = control;
		return record;
	}

	public static void Close()
	{
		FreeConsole();
	}
}
"@

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

function Invoke-VisualTmux([string[]]$Arguments) {
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($Arguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	$process = [System.Diagnostics.Process]::Start($psi)
	if (-not $process.WaitForExit(10000)) {
		$process.Kill()
		throw "tmux command timed out: $($Arguments -join ' ')"
	}
	return $process.ExitCode
}

$screen = ""
$ok = $false
$tmuxProcess = $null
try {
	New-Item -ItemType Directory -Force -Path $TempDirectory | Out-Null
	@("@echo off", "echo $Marker") |
	    Set-Content -LiteralPath $MarkerScript -Encoding ascii
	if (-not $DirectNoArgs) {
		Invoke-VisualTmux @("-L", $ServerName, "-f", "NUL",
		    "new-session", "-d", "-s", "visual", "cmd.exe") |
		    Out-Null
	}
	[TmuxVisualConsole]::Open("tmux visual verification", $Width, $Height)
	if ($DirectNoArgs) {
		$arguments = @()
	} else {
		$arguments = @("-L", $ServerName, "-f", "NUL", "attach", "-t",
		    "visual")
	}
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($arguments | ForEach-Object {
	    ConvertTo-WindowsArgument $_
	}) -join " "
	$psi.UseShellExecute = $false
	$psi.RedirectStandardInput = $false
	$psi.RedirectStandardOutput = $false
	$psi.RedirectStandardError = $false
	$tmuxProcess = [System.Diagnostics.Process]::Start($psi)

	Start-Sleep -Milliseconds 1500
	if ($DirectNoArgs) {
		[TmuxVisualConsole]::SendText("echo $Marker`r")
	} else {
		[TmuxVisualConsole]::SendText("`"$MarkerScript`"`r")
	}
	Start-Sleep -Milliseconds 1500
	try {
		$screen = [TmuxVisualConsole]::ReadVisibleText()
		$ok = $screen -like "*$Marker*"
	} catch {
		$screen = "console_buffer_error=$($_.Exception.Message)"
		$ok = $false
	}
	Start-Sleep -Seconds $HoldSeconds
} finally {
	try {
		if ($DirectNoArgs) {
			Invoke-VisualTmux @("kill-server") | Out-Null
		} else {
			Invoke-VisualTmux @("-L", $ServerName, "-f", "NUL",
			    "kill-server") | Out-Null
		}
	} catch {
	}
	if ($tmuxProcess -ne $null -and -not $tmuxProcess.HasExited) {
		try {
			$tmuxProcess.Kill()
		} catch {
		}
	}
	$resultDirectory = Split-Path -Parent $ResultPath
	if (-not [string]::IsNullOrWhiteSpace($resultDirectory)) {
		New-Item -ItemType Directory -Force -Path $resultDirectory |
		    Out-Null
	}
	@(
	    "ok=$ok",
	    "no_assert=$NoAssert",
	    "marker=$Marker",
	    "marker_script=$MarkerScript",
	    "tmux=$Tmux",
	    "server=$ServerName",
	    "direct_no_args=$DirectNoArgs",
	    "screen:",
	    $screen
	) | Set-Content -LiteralPath $ResultPath -Encoding utf8
	if (Test-Path -LiteralPath $TempDirectory) {
		try {
			Remove-Item -LiteralPath $TempDirectory -Recurse -Force
		} catch {
		}
	}
}

if (-not $ok -and -not $NoAssert) {
	throw "visible console did not show marker: $Marker"
}
try {
	Add-Content -LiteralPath $ResultPath -Value "after_assert=True" `
	    -Encoding utf8
} catch {
}
try {
	Write-Host "visual_console_verify=passed"
	Write-Host "result=$ResultPath"
} catch {
}
exit 0
