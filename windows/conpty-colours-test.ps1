param([Parameter(Mandatory = $true)][string]$Tmux, [switch]$ConsoleRecords)

$ErrorActionPreference = 'Stop'
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path
$socket = 'conpty-colours-' + [Guid]::NewGuid().ToString('N')
$probe = @'
Add-Type -Name Console -Namespace Probe -MemberDefinition @"
[DllImport("kernel32.dll")] public static extern IntPtr GetStdHandle(int id);
[DllImport("kernel32.dll")] public static extern bool GetConsoleMode(IntPtr handle, out uint mode);
[DllImport("kernel32.dll")] public static extern bool SetConsoleMode(IntPtr handle, uint mode);
[DllImport("kernel32.dll")] public static extern bool ReadFile(IntPtr handle, byte[] buffer, uint length, out uint count, IntPtr overlapped);
[DllImport("kernel32.dll")] public static extern bool WriteFile(IntPtr handle, byte[] buffer, uint length, out uint count, IntPtr overlapped);
[StructLayout(LayoutKind.Explicit, Size = 20)] public struct Record {
    [FieldOffset(0)] public ushort Type;
    [FieldOffset(4)] public int Down;
    [FieldOffset(14)] public ushort Character;
}
[DllImport("kernel32.dll")] public static extern bool ReadConsoleInputW(IntPtr handle, out Record record, uint length, out uint count);
"@
$useRecords = __RECORDS__
$inputHandle = [Probe.Console]::GetStdHandle(-10)
$outputHandle = [Probe.Console]::GetStdHandle(-11)
[uint32]$mode = 0
if (![Probe.Console]::SetConsoleMode($inputHandle, $(if ($useRecords) { 0 } else { 0x200 })) -or
    ![Probe.Console]::GetConsoleMode($outputHandle, [ref]$mode) -or
    ![Probe.Console]::SetConsoleMode($outputHandle, ($mode -bor 4))) {
    throw 'Could not enable virtual-terminal mode.'
}
$esc = [string][char]27
$st = $esc + '\'
$request = [Text.Encoding]::ASCII.GetBytes("$esc]10;?$st$esc]11;?$st")
[uint32]$count = 0
if (![Probe.Console]::WriteFile($outputHandle, $request, $request.Length, [ref]$count, [IntPtr]::Zero) -or $count -ne $request.Length) {
    throw 'Could not write the colour queries.'
}
[Console]::WriteLine('COLOUR_READY')
$buffer = [byte[]]::new(256)
$reply = ''
do {
    if ($useRecords) {
        $record = [Probe.Console+Record]::new()
        if (![Probe.Console]::ReadConsoleInputW($inputHandle, [ref]$record, 1, [ref]$count)) {
            throw 'Could not read a console input record.'
        }
        if ($record.Type -eq 1 -and $record.Down -ne 0 -and $record.Character -ne 0) {
            $reply += [char]$record.Character
        }
    } else {
        if (![Probe.Console]::ReadFile($inputHandle, $buffer, $buffer.Length, [ref]$count, [IntPtr]::Zero)) {
            throw 'Could not read the colour reply.'
        }
        $reply += [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
    }
    $foreground = [regex]::Match($reply, '\x1b\]10;(rgb:[\da-f/]+)(?:\x07|\x1b\\)', 'IgnoreCase')
    $background = [regex]::Match($reply, '\x1b\]11;(rgb:[\da-f/]+)(?:\x07|\x1b\\)', 'IgnoreCase')
} until ($foreground.Success -and $background.Success)
[Console]::WriteLine('COLOUR_10 ' + $foreground.Groups[1].Value)
[Console]::WriteLine('COLOUR_11 ' + $background.Groups[1].Value)
Start-Sleep -Seconds 10
'@
$probe = $probe.Replace('__RECORDS__', $(if ($ConsoleRecords) { '$true' } else { '$false' }))
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($probe))

try {
    & $Tmux -L $socket -f NUL new-session -d -s probe
    if ($LASTEXITCODE -ne 0) { throw 'Could not start the test server.' }
    & $Tmux -L $socket set-option -w -t probe window-style 'fg=#aabbcc,bg=#112233'
    if ($LASTEXITCODE -ne 0) { throw 'Could not set the test colours.' }
    & $Tmux -L $socket respawn-pane -k -t probe:0.0 "powershell.exe -NoProfile -EncodedCommand $encoded"
    if ($LASTEXITCODE -ne 0) { throw 'Could not start the colour probe.' }
    $deadline = [DateTime]::UtcNow.AddSeconds(6)
    do {
        $pane = & $Tmux -L $socket capture-pane -p -t probe:0.0
        if (($pane -match '^COLOUR_10 rgb:aaaa/bbbb/cccc$') -and
            ($pane -match '^COLOUR_11 rgb:1111/2222/3333$')) {
            Write-Output 'The pane received the configured foreground and background colour replies.'
            return
        }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected the pane to receive both colour replies. Pane output: $($pane -join ' ')"
} finally {
    & $Tmux -L $socket kill-server
}
