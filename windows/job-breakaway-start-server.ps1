<#
.SYNOPSIS
Stress test for Job Object breakaway in the active server startup path (P1-8).

Verifies that tmux server can start both from a normal shell and from
within a restrictive Job Object that does not permit child breakaway.
#>
param(
  [string]$Tmux = '.\tmux.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Tmux)) {
  $Tmux = '.\dist\tmux-win32-portable\tmux.exe'
}
if (-not (Test-Path $Tmux)) {
  throw 'tmux.exe not found'
}

# Test 1: Direct start
$socket = "jobbreak-$PID"
& $Tmux -L $socket new-session -d -s J "echo OK"
if ($LASTEXITCODE -ne 0) { throw 'direct start failed' }
& $Tmux -L $socket kill-server

Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class RestrictedJobRunner {
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct STARTUPINFO {
    public int cb;
    public string reserved;
    public string desktop;
    public string title;
    public int x, y, xSize, ySize, xChars, yChars, fillAttribute, flags;
    public short showWindow, reserved2;
    public IntPtr reserved2Ptr, stdInput, stdOutput, stdError;
  }

  [StructLayout(LayoutKind.Sequential)]
  struct PROCESS_INFORMATION {
    public IntPtr process, thread;
    public int processId, threadId;
  }

  [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  static extern bool CreateProcess(string app, string commandLine,
    IntPtr processAttributes, IntPtr threadAttributes, bool inheritHandles,
    int creationFlags, IntPtr environment, string currentDirectory,
    ref STARTUPINFO startupInfo, out PROCESS_INFORMATION processInformation);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern IntPtr CreateJobObject(IntPtr attributes, string name);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern uint ResumeThread(IntPtr thread);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
  [DllImport("kernel32.dll", SetLastError = true)]
  static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
  [DllImport("kernel32.dll")]
  static extern bool CloseHandle(IntPtr handle);

  public static int Run(string app, string arguments, string cwd) {
    const int CREATE_SUSPENDED = 0x4;
    var startup = new STARTUPINFO();
    startup.cb = Marshal.SizeOf(startup);
    PROCESS_INFORMATION process;
    IntPtr job = CreateJobObject(IntPtr.Zero, null);
    if (job == IntPtr.Zero)
      throw new Win32Exception();
    try {
      string commandLine = "\"" + app + "\" " + arguments;
      if (!CreateProcess(app, commandLine, IntPtr.Zero, IntPtr.Zero, false,
          CREATE_SUSPENDED, IntPtr.Zero, cwd, ref startup, out process))
        throw new Win32Exception();
      try {
        if (!AssignProcessToJobObject(job, process.process))
          throw new Win32Exception();
        if (ResumeThread(process.thread) == 0xffffffff)
          throw new Win32Exception();
        if (WaitForSingleObject(process.process, 30000) != 0)
          throw new TimeoutException("restricted job child timed out");
        uint exitCode;
        if (!GetExitCodeProcess(process.process, out exitCode))
          throw new Win32Exception();
        return unchecked((int)exitCode);
      } finally {
        CloseHandle(process.thread);
        CloseHandle(process.process);
      }
    } finally {
      CloseHandle(job);
    }
  }
}
'@

$tmuxPath = (Resolve-Path $Tmux).Path
$innerSocket = "jobbreak-inner-$PID"
$arguments = "-L $innerSocket -f NUL new-session -d -s J cmd.exe"
$exitCode = [RestrictedJobRunner]::Run($tmuxPath, $arguments, (Get-Location).Path)
if ($exitCode -ne 0) { throw "restricted job start failed: $exitCode" }
& $Tmux -L $innerSocket kill-server

Write-Host 'job breakaway start-server smoke passed'
