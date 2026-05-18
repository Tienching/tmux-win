param(
	[Parameter(Mandatory = $true)]
	[string]$Tmux,
	[Parameter(Mandatory = $true)]
	[string]$ServerName,
	[Parameter(Mandatory = $true)]
	[string]$Session,
	[Parameter(Mandatory = $true)]
	[string]$Marker,
	[Parameter(Mandatory = $true)]
	[string]$StartedFile,
	[Parameter(Mandatory = $true)]
	[string]$InputFile,
	[Parameter(Mandatory = $true)]
	[string]$ExitFile,
	[Parameter(Mandatory = $true)]
	[string]$SizeFile,
	[int]$Width = 96,
	[int]$Height = 28,
	[int]$ResizeWidth = 0,
	[int]$ResizeHeight = 0,
	[string]$ResizedFile = "",
	[string]$ResizeMarker = "",
	[string]$ResizeSequence = "",
	[string]$ResizeLogFile = "",
	[string]$ResizeMarkerPrefix = "",
	[switch]$SkipInitialInput,
	[string]$CtrlCCommand = "",
	[string]$CtrlCFile = "",
	[string]$CtrlCMarker = "",
	[string]$CtrlBreakCommand = "",
	[string]$CtrlBreakFile = "",
	[string]$CtrlBreakMarker = ""
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class Win32ConsoleAttachProbe
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
	const uint CTRL_C_EVENT = 0;
	const uint CTRL_BREAK_EVENT = 1;
	const uint SHIFT_PRESSED = 0x0010;
	const uint LEFT_CTRL_PRESSED = 0x0008;
	const uint LEFT_ALT_PRESSED = 0x0002;
	const ushort VK_CANCEL = 0x03;
	const ushort VK_C = 0x43;
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

	public delegate bool ConsoleCtrlDelegate(uint type);
	static ConsoleCtrlDelegate ignoreControl = new ConsoleCtrlDelegate(
	    IgnoreControl);

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
	static extern bool WriteConsoleInputW(IntPtr hConsoleInput,
	    INPUT_RECORD[] lpBuffer, uint nLength, out uint lpNumberOfEventsWritten);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GenerateConsoleCtrlEvent(uint dwCtrlEvent,
	    uint dwProcessGroupId);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool SetConsoleCtrlHandler(ConsoleCtrlDelegate handler,
	    bool add);
	[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern short VkKeyScanW(char ch);
	[DllImport("user32.dll", SetLastError = true)]
	static extern uint MapVirtualKeyW(uint uCode, uint uMapType);

	public static string Open(int width, int height)
	{
		FreeConsole();
		if (!AllocConsole()) {
			int error = Marshal.GetLastWin32Error();
			throw new Win32Exception(error);
		}

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

		return SetSize(output, width, height);
	}

	public static string Resize(int width, int height)
	{
		IntPtr output = GetStdHandle(STD_OUTPUT_HANDLE);
		if (output == INVALID_HANDLE_VALUE || output == IntPtr.Zero)
			throw new Win32Exception(Marshal.GetLastWin32Error());
		return SetSize(output, width, height);
	}

	static string SetSize(IntPtr output, int width, int height)
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
		size.X = (short)Math.Max(width, before.dwSize.X);
		size.Y = (short)Math.Max(Math.Max(height, 100), before.dwSize.Y);
		if (!SetConsoleScreenBufferSize(output, size))
			throw new Win32Exception(Marshal.GetLastWin32Error(),
			    "SetConsoleScreenBufferSize " + size.X.ToString() +
			    "x" + size.Y.ToString());
		SetWindow(output, width, height);
		return CurrentSize(output);
	}

	static void SetWindow(IntPtr output, int width, int height)
	{
		SMALL_RECT rect = new SMALL_RECT();
		rect.Left = 0;
		rect.Top = 0;
		rect.Right = (short)(width - 1);
		rect.Bottom = (short)(height - 1);
		if (!SetConsoleWindowInfo(output, true, ref rect))
			throw new Win32Exception(Marshal.GetLastWin32Error(),
			    "SetConsoleWindowInfo " + width.ToString() +
			    "x" + height.ToString());
	}

	static string CurrentSize(IntPtr output)
	{
		CONSOLE_SCREEN_BUFFER_INFO info;
		if (!GetConsoleScreenBufferInfo(output, out info))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		int actualWidth = info.srWindow.Right - info.srWindow.Left + 1;
		int actualHeight = info.srWindow.Bottom - info.srWindow.Top + 1;
		return actualWidth.ToString() + "x" + actualHeight.ToString();
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
			throw new InvalidOperationException("short console input write");
	}

	public static void SendCtrlC()
	{
		if (!GenerateControl(CTRL_C_EVENT)) {
			ushort scan = (ushort)MapVirtualKeyW(VK_C, 0);
			SendKey('\x03', VK_C, scan, LEFT_CTRL_PRESSED);
		}
	}

	public static void SendCtrlBreak()
	{
		if (!GenerateControl(CTRL_BREAK_EVENT)) {
			ushort scan = (ushort)MapVirtualKeyW(VK_CANCEL, 0);
			SendKey('\0', VK_CANCEL, scan, LEFT_CTRL_PRESSED);
		}
	}

	static void SendKey(char ch, ushort vk, ushort scan, uint control)
	{
		IntPtr input = GetStdHandle(STD_INPUT_HANDLE);
		if (input == INVALID_HANDLE_VALUE || input == IntPtr.Zero)
			throw new Win32Exception(Marshal.GetLastWin32Error());

		INPUT_RECORD[] records = new INPUT_RECORD[2];
		records[0] = KeyRecord(ch, vk, scan, control, true);
		records[1] = KeyRecord(ch, vk, scan, control, false);
		uint written;
		if (!WriteConsoleInputW(input, records, (uint)records.Length,
		    out written))
			throw new Win32Exception(Marshal.GetLastWin32Error());
		if (written != records.Length)
			throw new InvalidOperationException("short console input write");
	}

	static bool IgnoreControl(uint type)
	{
		return true;
	}

	static bool GenerateControl(uint type)
	{
		SetConsoleCtrlHandler(ignoreControl, true);
		bool generated = GenerateConsoleCtrlEvent(type, 0);
		System.Threading.Thread.Sleep(100);
		SetConsoleCtrlHandler(ignoreControl, false);
		return generated;
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

$exitCode = 1
try {
	$actualSize = [Win32ConsoleAttachProbe]::Open($Width, $Height)
	Set-Content -LiteralPath $SizeFile -Encoding ascii -Value $actualSize

	$arguments = @("-L", $ServerName, "-f", "NUL", "attach", "-t",
	    $Session)
	$psi = [System.Diagnostics.ProcessStartInfo]::new()
	$psi.FileName = $Tmux
	$psi.Arguments = ($arguments | ForEach-Object {
	    if ($_ -match '[\s"]') {
		    '"' + $_.Replace('"', '\"') + '"'
	    } else {
		    $_
	    }
	}) -join " "
	$psi.UseShellExecute = $false
	$psi.RedirectStandardInput = $false
	$psi.RedirectStandardOutput = $false
	$psi.RedirectStandardError = $false

	$process = [System.Diagnostics.Process]::Start($psi)
	Set-Content -LiteralPath $StartedFile -Encoding ascii -Value `
	    $process.Id
	Start-Sleep -Milliseconds 1500
	if ($SkipInitialInput) {
		Set-Content -LiteralPath $InputFile -Encoding ascii -Value `
		    "skipped"
	} else {
		[Win32ConsoleAttachProbe]::SendText("echo $Marker`r")
		Set-Content -LiteralPath $InputFile -Encoding ascii -Value `
		    "sent"
	}
	if (-not [string]::IsNullOrEmpty($CtrlCFile)) {
		if (-not [string]::IsNullOrEmpty($CtrlCCommand)) {
			Start-Sleep -Milliseconds 1200
			[Win32ConsoleAttachProbe]::SendText("$CtrlCCommand`r")
		}
		Start-Sleep -Milliseconds 2000
		[Win32ConsoleAttachProbe]::SendCtrlC()
		Set-Content -LiteralPath $CtrlCFile -Encoding ascii -Value `
		    "sent"
		if (-not [string]::IsNullOrEmpty($CtrlCMarker)) {
			Start-Sleep -Milliseconds 1200
			[Win32ConsoleAttachProbe]::SendText(
			    "echo $CtrlCMarker`r")
		}
	}
	if (-not [string]::IsNullOrEmpty($CtrlBreakFile)) {
		if (-not [string]::IsNullOrEmpty($CtrlBreakCommand)) {
			Start-Sleep -Milliseconds 1200
			[Win32ConsoleAttachProbe]::SendText(
			    "$CtrlBreakCommand`r")
		}
		Start-Sleep -Milliseconds 2000
		[Win32ConsoleAttachProbe]::SendCtrlBreak()
		Set-Content -LiteralPath $CtrlBreakFile -Encoding ascii `
		    -Value "sent"
		if (-not [string]::IsNullOrEmpty($CtrlBreakMarker)) {
			Start-Sleep -Milliseconds 1200
			[Win32ConsoleAttachProbe]::SendText(
			    "echo $CtrlBreakMarker`r")
		}
	}
	if ($ResizeWidth -gt 0 -and $ResizeHeight -gt 0 -and
	    -not [string]::IsNullOrEmpty($ResizedFile)) {
		Start-Sleep -Milliseconds 1500
		$resizedSize = [Win32ConsoleAttachProbe]::Resize(
		    $ResizeWidth, $ResizeHeight)
		Set-Content -LiteralPath $ResizedFile -Encoding ascii `
		    -Value $resizedSize
		if (-not [string]::IsNullOrEmpty($ResizeMarker)) {
			Start-Sleep -Milliseconds 1500
			[Win32ConsoleAttachProbe]::SendText(
			    "echo $ResizeMarker`r")
		}
	}
	if (-not [string]::IsNullOrWhiteSpace($ResizeSequence)) {
		$resizeLog = [System.Collections.Generic.List[string]]::new()
		$resizeEntries = @($ResizeSequence -split "," |
		    Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
		for ($i = 0; $i -lt $resizeEntries.Count; $i++) {
			$entry = $resizeEntries[$i].Trim()
			if ($entry -notmatch "^([0-9]+)x([0-9]+)$") {
				throw "invalid resize sequence entry: $entry"
			}
			Start-Sleep -Milliseconds 900
			$sequenceSize = [Win32ConsoleAttachProbe]::Resize(
			    [int]$Matches[1], [int]$Matches[2])
			$sequenceMarker = ""
			if (-not [string]::IsNullOrEmpty($ResizeMarkerPrefix)) {
				$sequenceMarker = "$ResizeMarkerPrefix$i"
				Start-Sleep -Milliseconds 900
				[Win32ConsoleAttachProbe]::SendText(
				    "echo $sequenceMarker`r")
			}
			$resizeLog.Add(("{0}:{1}:{2}" -f $i, $sequenceSize,
			    $sequenceMarker))
		}
		if (-not [string]::IsNullOrEmpty($ResizeLogFile)) {
			Set-Content -LiteralPath $ResizeLogFile -Encoding ascii `
			    -Value $resizeLog
		}
	}

	$process.WaitForExit()
	$exitCode = $process.ExitCode
} catch {
	Set-Content -LiteralPath $InputFile -Encoding utf8 -Value `
	    ("error: " + $_.Exception.ToString())
	$exitCode = 1
} finally {
	Set-Content -LiteralPath $ExitFile -Encoding ascii -Value $exitCode
	[Win32ConsoleAttachProbe]::Close()
}
exit $exitCode
