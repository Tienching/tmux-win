param(
    [Parameter(Mandatory = $true)]
    [string]$Tmux,
    [switch]$Attach
)

$ErrorActionPreference = 'Stop'
$Tmux = (Resolve-Path -LiteralPath $Tmux).Path
$socket = 'conpty-keys-' + [guid]::NewGuid().ToString('N')
$pane = 'probe:0.0'
$cases = [ordered]@{
    'Enter'       = 'Enter 0D 0'
    'C-Enter'     = 'Enter 0A 4'
    'S-Enter'     = 'Enter 0D 2'
    'M-Enter'     = 'Enter 0D 1'
    'C-S-Enter'   = 'Enter 0A 6'
    'C-M-Enter'   = 'Enter 0A 5'
    'C-M-S-Enter' = 'Enter 0A 7'
    'C-j'         = 'J 0A 4'
    'C-m'         = 'M 0D 4'
    'C-a'         = 'A 01 4'
    'C-c'         = 'C 03 4'
    'C-i'         = 'I 09 4'
    'Tab'         = 'Tab 09 0'
    'C-Tab'       = 'Tab 09 4'
    'S-Tab'       = 'Tab 09 2'
    'BTab'        = 'Tab 09 2'
    'C-BTab'      = 'Tab 09 6'
    'C-Space'     = 'Spacebar 00 4'
    'S-Space'     = 'Spacebar 20 2'
    'S-Escape'    = 'Escape 1B 2'
    'C-Left'      = 'LeftArrow 00 4'
    'C-M-S-Right' = 'RightArrow 00 7'
    'S-F1'        = 'F1 00 2'
    'C-M-S-F12'   = 'F12 00 7'
    'C-S-Delete'  = 'Delete 00 6'
    'C-S-BSpace'  = 'Backspace 7F 6'
    'M-KPEnter'   = 'Enter 0D 1'
    'C-KP1'       = 'NumPad1 31 4'
    'S-KP+'       = 'Add 2B 2'
    'S-a'         = 'A 41 2'
    'M-a'         = 'A 61 1'
}
$nativeCases = [ordered]@{
    '13:13:0'  = 'Enter 0D 0'
    '13:13:8'  = 'Enter 0D 4'
    '74:10:8'  = 'J 0A 4'
    '13:13:16' = 'Enter 0D 2'
    '9:9:8'    = 'Tab 09 4'
    '39:0:282' = 'RightArrow 00 7'
    '65:65:16' = 'A 41 2'
    '50:0:8'   = 'D2 00 4'
    '124:0:8'  = 'F13 00 4'
    '90:122:0' = 'Z 7A 0'
}
if ($Attach) {
    $labels = @($nativeCases.Keys)
    $expected = @($nativeCases.Values)
} else {
    $labels = @($cases.Keys) + 'literal LF'
    $expected = @($cases.Values) + 'Enter 0A 4'
}
$reader = @'
Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class PaneKeyProbe {
    [StructLayout(LayoutKind.Explicit, Size = 20)]
    struct InputRecord {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(4)] public int KeyDown;
        [FieldOffset(10)] public ushort VirtualKey;
        [FieldOffset(14)] public ushort Character;
        [FieldOffset(16)] public uint State;
    }

    [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int id);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadConsoleInputW(IntPtr input, out InputRecord record,
        uint count, out uint read);

    public static string NextKey() {
        for (;;) {
            InputRecord record;
            uint read;
            if (!ReadConsoleInputW(GetStdHandle(-10), out record, 1, out read))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            if (read == 0 || record.Type != 1 || record.KeyDown == 0)
                continue;
            if ((record.VirtualKey >= 16 && record.VirtualKey <= 18) ||
                (record.VirtualKey >= 160 && record.VirtualKey <= 165))
                continue;
            int modifiers = (record.State & 3) != 0 ? 1 : 0;
            if ((record.State & 16) != 0) modifiers |= 2;
            if ((record.State & 12) != 0) modifiers |= 4;
            return String.Format("{0} {1:X2} {2}", (ConsoleKey)record.VirtualKey,
                record.Character, modifiers);
        }
    }
}
"@
[Console]::TreatControlCAsInput = $true
[Console]::WriteLine('KEY_PROBE_READY')
for ($i = 0; $i -lt __COUNT__; ++$i) {
    [Console]::WriteLine('KEY_PROBE ' + [PaneKeyProbe]::NextKey())
}
Start-Sleep -Seconds 15
'@
$reader = $reader.Replace('__COUNT__', [string]$expected.Count)
$command = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($reader))
$probe = $null
$probeFiles = @{}

try {
    & $Tmux -L $socket -f NUL new-session -d -s probe "powershell.exe -NoProfile -EncodedCommand $command"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to start the test server' }

    $ready = $false
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        if ((& $Tmux -L $socket capture-pane -p -S - -t $pane) -match '^KEY_PROBE_READY') {
            $ready = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (!$ready) { throw 'The console input probe did not start' }

    if ($Attach) {
        & $Tmux -L $socket bind-key -n C-F1 set-environment -g NATIVE_KEY_BOUND yes
        if ($LASTEXITCODE -ne 0) { throw 'Failed to bind the native shortcut' }
        foreach ($name in 'StartedFile', 'InputFile', 'ExitFile', 'SizeFile') {
            $probeFiles[$name] = Join-Path ([IO.Path]::GetTempPath()) "$socket-$name.txt"
        }
        $probeScript = Join-Path $PSScriptRoot 'console-attach-probe.ps1'
        $records = @($nativeCases.Keys)
        $records[-1] = '112:0:8,' + $records[-1]
        $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$probeScript`"",
            '-Tmux', "`"$Tmux`"", '-ServerName', $socket, '-Session', 'probe', '-Marker', 'unused',
            '-KeyRecords', ($records -join ','))
        foreach ($name in $probeFiles.Keys) {
            $arguments += "-$name", "`"$($probeFiles[$name])`""
        }
        $probe = Start-Process powershell.exe -ArgumentList $arguments -WindowStyle Hidden -PassThru
    } else {
        $keys = @($cases.Keys)
        & $Tmux -L $socket send-keys -t $pane @keys
        if ($LASTEXITCODE -ne 0) { throw 'Failed to send the named keys' }
        & $Tmux -L $socket send-keys -H -t $pane 0A
        if ($LASTEXITCODE -ne 0) { throw 'Failed to send literal LF' }
    }

    $actual = @()
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        $lines = & $Tmux -L $socket capture-pane -p -S - -t $pane
        $actual = @($lines -match '^KEY_PROBE ' -replace '^KEY_PROBE ', '')
        if ($actual.Count -ge $expected.Count) { break }
        Start-Sleep -Milliseconds 100
    }

    if (($actual -join "`n") -cne ($expected -join "`n")) {
        if ($Attach -and (Test-Path -LiteralPath $probeFiles.InputFile)) {
            Get-Content -LiteralPath $probeFiles.InputFile | Write-Output
        }
        for ($i = 0; $i -lt [Math]::Max($expected.Count, $actual.Count); ++$i) {
            $label = $labels[$i]
            Write-Output ('{0,-14} expected {1,-20} received {2}' -f $label, $expected[$i], $actual[$i])
        }
        throw 'The pane received unexpected console key events'
    }
    if ($Attach -and (& $Tmux -L $socket show-environment -g NATIVE_KEY_BOUND) -ne 'NATIVE_KEY_BOUND=yes') {
        throw 'tmux did not handle the bound native shortcut'
    }
    Write-Output "Verified $($expected.Count) key events in the pane"
} finally {
    & $Tmux -L $socket kill-server
    if ($null -ne $probe) {
        if (!$probe.WaitForExit(5000)) { $probe.Kill() }
        foreach ($path in $probeFiles.Values) {
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
        }
    }
}
