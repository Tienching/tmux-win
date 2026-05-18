# Windows verification guide

This guide covers local checks for the native Windows tmux port.

## Quick portable check

Use this after building or unpacking the portable directory:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\verify-portable.ps1 -Tmux .\dist\tmux-win32-portable\tmux.exe
```

This verifies `tmux -V`, detached session creation, pane input/output, command
clients, and `kill-server`.

## Graphical attach check

Use this when you need to confirm what is actually visible in Windows Terminal:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\visual-terminal-verify.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -ScreenshotPath .\dist\visual-terminal-verify-latest.png
```

The check opens a real visible terminal titled `tmux visual verification`,
attaches a tmux client, runs a marker command inside the pane, then reads the
Windows Terminal UI with UI Automation. A pass prints
`visual_terminal_verify=passed` and writes
`dist\visual-terminal-verify-latest.txt` with `ok=True` plus the visible
marker line. When `-ScreenshotPath` is supplied, it also saves a PNG of the
terminal window and records the screenshot SHA256 in the result file.

## Interactive attach check

Start from a real terminal such as Windows Terminal or a classic PowerShell
console:

```powershell
cd D:\Users\jonaszchen\Documents\tmux\dist\tmux-win32-portable
.\tmux.exe
```

A successful attach takes over the current console. The default pane may be
`cmd.exe`, so the prompt can look similar to the directory where tmux was
started. If the prompt changes from `PS ...>` to `D:\...\>` and accepts input,
tmux is attached rather than hung. Type a command such as:

```cmd
echo TMUX_INTERACTIVE_OK
```

If the command prints `TMUX_INTERACTIVE_OK`, the client and pane are working.

Detach back to the outer shell with:

```text
Ctrl+b then d
```

Reattach with:

```powershell
.\tmux.exe attach
```

Stop the default server with:

```powershell
.\tmux.exe kill-server
```

## Appears stuck after start

A bare `.\tmux.exe` is an attached foreground client. It does not return to the
outer PowerShell prompt until you detach or exit. If the screen shows a prompt
such as `D:\...\>` and accepts typed commands, it is usually the tmux pane's
default `cmd.exe`, not a hung client.

From another terminal, inspect the default server:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\diagnose-console.ps1 -Tmux .\dist\tmux-win32-portable\tmux.exe
```

If the diagnostic reports `diagnosis=attached_client_present`, use `Ctrl+b`
then `d` in the attached terminal, or stop the default server with:

```powershell
cd D:\Users\jonaszchen\Documents\tmux\dist\tmux-win32-portable
.\tmux.exe kill-server
```

## PowerShell pane check

To verify an interactive PowerShell pane explicitly:

```powershell
cd D:\Users\jonaszchen\Documents\tmux\dist\tmux-win32-portable
.\tmux.exe -L manual-ps -f NUL new-session -s test powershell.exe
```

Detach with `Ctrl+b` then `d`, then clean up:

```powershell
.\tmux.exe -L manual-ps -f NUL kill-server
```

## Console diagnostics

Use this when a bare `tmux.exe` exits, reports `lost tty`, or appears not to
attach:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\diagnose-console.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -ResetDefault `
  -RunQuickVerify
```

The diagnostic records console handle type/mode, terminal environment,
endpoint state, running tmux processes, and a fresh manual attach command.

## Runtime smoke

Run the full local runtime smoke against a portable package:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\smoke-runtime.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -TimeoutSeconds 60
```

This covers server lifecycle, panes, respawn, kill cleanup, copy mode, buffers,
control mode, popups/menus/prompts, clipboard, and real-console attach
scenarios.

## Respawn regression check

Run this when changing ConPTY close, process cleanup, pane destruction, or
respawn code:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\respawn-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 20 `
  -TimeoutSeconds 60
```

It repeatedly exercises `respawn-pane -k` and verifies the restarted pane still
accepts input.

## Job regression check

Run this when changing job, `run-shell`, process cleanup, or stdout/stderr
handling:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\job-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 10 `
  -BackgroundJobs 8 `
  -TimeoutSeconds 60
```

It repeatedly exercises `run-shell -E`, concurrent background `run-shell -b`
jobs, and long background job cleanup during `kill-server`.

## IPC ACL regression check

Run this when changing Windows IPC, endpoint creation, or startup cleanup:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\ipc-acl-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 3 `
  -TimeoutSeconds 20
```

It verifies the endpoint ACL grants only the current user, validates endpoint
format, sends a bad IPC token over a raw TCP connection, and confirms the server
continues accepting a valid client afterward.

## IPC boundary audit

Run this when changing Windows IPC security or release-audit coverage:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\ipc-boundary-audit.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\ipc-boundary-audit.json
```

This writes a JSON summary for endpoint owner/DACL checks, bad-token rejection,
and valid reconnect after a rejected token. In an elevated PowerShell, add
`-RunSystemTaskProbe` to create a temporary SYSTEM scheduled task and verify a
service-like context cannot read the endpoint token. To cover a real second
local or domain account, pass `-OtherUserCredential (Get-Credential)`. On an
elevated local validation host, `-CreateTemporaryLocalUser` creates a temporary
local account for the probe and deletes it before the script exits.

## Client lifecycle regression check

Run this when changing IPC, client attach/detach, or control-mode plumbing:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\client-lifecycle-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 5 `
  -CommandClients 8 `
  -TimeoutSeconds 60
```

It repeatedly exercises concurrent command clients, a control-mode client, a
redirected attached client, detach, and final server cleanup.

## Signal matrix regression check

Run this when changing Windows console input, process-group signals, or raw
input handling:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\signal-matrix-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 3 `
  -TimeoutSeconds 60
```

It repeatedly exercises pane-delivered `C-c`, `C-Break`, cmd-hosted and
PowerShell child interruption, and raw `C-c` delivery as an ETX byte.

## Config parser regression check

Run this when changing config parsing, `source-file`, Windows path expansion,
hooks, or format handling:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\config-parser-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -TimeoutSeconds 60
```

It exercises semicolon-separated config commands, nested `source-file`, `%ENV%`
source globs, paths with spaces and shell metacharacters, hooks, `if-shell`,
key bindings, and format-bearing option values.

## Linux surface parity matrix

When WSL has a Linux `tmux` installed, compare the Linux command, option, and
key-table surface against the Windows portable binary:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\linux-parity-matrix.ps1 `
  -WindowsTmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\linux-parity-matrix.json
```

This starts isolated Windows and Linux tmux servers, exports `list-commands`,
global/server/window options, key tables, and key-binding counts, then records
Linux items missing on Windows. It is a surface matrix, not a complete
behavioral parity proof.

To run a focused behavior matrix against WSL and Windows:

```powershell
.\windows\linux-behavior-parity.ps1 `
  -WindowsTmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\linux-behavior-parity.json
```

This runs matching Windows and Linux tmux workflows for session/window/pane
state, buffers including append/list/delete/file round trips and save append, options including set/show and unset/default behavior, environment, `new-window -c`, `run-shell -c`, and dynamic pane cwd
selection, select-window/last-window/select-pane/last-pane active state, next/previous-window navigation, resize-pane zoom toggling, format expansion, pane current command/path formats, command workflows including `run-shell -b`,
`source-file` configuration loading, `pipe-pane` output/input, copy-mode
copy/search/history plus multi-line and rectangle selection behavior, hooks,
key binding bind/list/unbind, control mode, pane reordering, pane input/capture including history range, paste-buffer,
and `wait-for`. The JSON summary includes `CategoryCoverage` for the required
behavior categories, including `paths`, `configuration`, `key-bindings`, and `copy-mode`, and
`verify-release-artifacts.ps1 -RequireLinuxBehaviorParity` rejects summaries
that do not cover every required category.

## Hosted CI audit

To record whether the GitHub repository has a green hosted Windows workflow for
the checked-out commit:

```powershell
$headSha = (& git rev-parse HEAD).Trim()
.\windows\hosted-ci-audit.ps1 `
  -HeadSha $headSha `
  -Output .\dist\hosted-ci-audit.json
```

This queries the GitHub Actions workflow list for the origin repository and
records whether the expected Windows workflow exists and has a successful run
for that head SHA. The script also accepts `-Branch` and `-RunLimit` when the
release validation needs to narrow or broaden the workflow-run search.
Set `GH_TOKEN` or `GITHUB_TOKEN` for local release validation to avoid
anonymous GitHub API rate limits; the hosted workflows provide `GH_TOKEN`
automatically.
The JSON also records whether the expected workflow file exists in the local
checkout, which distinguishes an unpublished local workflow from a missing
local workflow file.

## Source state audit

To record whether artifacts were produced from a clean committed source tree:

```powershell
.\windows\source-state-audit.ps1 `
  -Output .\dist\source-state-audit.json
```

Use `-RequireClean` on production release builders and hosted CI. Local dirty
trees are allowed during development, but they are not completion evidence for
published release artifacts. The JSON also includes a tracked-diff SHA256,
untracked-file hashes, and a combined `SourceStateFingerprint` so dirty
development artifacts can be compared against the exact local source state that
produced them.

## Signing audit

To record the MSIX Authenticode signing state:

```powershell
.\windows\signing-audit.ps1 `
  -Msix .\dist\tmux-win32.msix `
  -MsixSummary .\dist\tmux-win32.msix.json `
  -Output .\dist\signing-audit.json
```

This writes whether the package is unsigned, signed-but-untrusted, or trusted.
It also records whether the MSIX summary SHA256 matches the actual package,
whether the summary Publisher matches the package manifest Publisher, and
whether a signer subject matches the manifest Publisher.
It also inventories local personal-store code-signing certificates, including
private-key availability, validity window, and whether any certificate subject
matches the MSIX Publisher.
`verify-release-artifacts.ps1 -RequireSigningAudit` rejects those metadata
mismatches independently of whether production trusted signing is required.

## Release gate

The current local release gate is:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\release-check.ps1 `
  -SkipBuild `
  -RespawnIterations 20 `
  -IpcAclIterations 3 `
  -JobStressIterations 10 `
  -ClientStressIterations 5 `
  -SignalMatrixIterations 3 `
  -RunConfigStress `
  -StressIterations 1 `
  -SoakSeconds 10 `
  -ConsoleSoakSeconds 10 `
  -ConsoleReattachCycles 2 `
  -ClipboardStressIterations 3 `
  -RunVisualTerminalVerify `
  -BuildMsix
```

For a clean build, remove `-SkipBuild` and make sure MSYS2 MinGW64 tools are on
`PATH`.

To write a machine-readable completion audit without claiming completion:

```powershell
.\windows\completion-audit.ps1 `
  -ReleaseSummary .\dist\release-check.json `
  -CommandSurfaceSummary .\dist\command-surface.json `
  -MsixSummary .\dist\tmux-win32.msix.json `
  -VisualTerminalSummary .\dist\visual-terminal-verify.txt `
  -SigningSummary .\dist\signing-audit.json `
  -IpcBoundarySummary .\dist\ipc-boundary-audit.json `
  -LinuxParitySummary .\dist\linux-parity-matrix.json `
  -LinuxBehaviorSummary .\dist\linux-behavior-parity.json `
  -HostedCiSummary .\dist\hosted-ci-audit.json `
  -SourceStateSummary .\dist\source-state-audit.json `
  -Output .\dist\completion-audit.json
```

After those evidence files exist, verify the extended artifact set:

```powershell
.\windows\verify-release-artifacts.ps1 `
  -RequireMsix `
  -RequireSigningAudit `
  -RequireCompletionAudit `
  -RequireIpcBoundaryAudit `
  -RequireLinuxParity `
  -RequireLinuxBehaviorParity `
  -RequireHostedCiAudit `
  -RequireSourceStateAudit
```

With `-RequireHostedCiAudit`, the hosted CI summary must include a non-empty
target `HeadSha`; if the audit reports `passed`, the recorded green run must
match that SHA.
Use `-RequireHostedCiGreen` when the caller specifically needs to reject
anything except an observed successful hosted workflow run for that target
commit.

With `-RequireCompletionAudit`, the verifier also enforces the release-gate
steps and stress minimums from `windows/RELEASE.md`: build, package smoke,
zip and manifest checks, command surface, MSIX packaging, install/uninstall,
respawn 20, IPC ACL 3, job stress 10, client lifecycle 5, signal matrix 3,
packaged stress 1, mixed soak 10 seconds, console soak 10 seconds, console
reattach 2, clipboard stress 3, and config parser stress enabled.

Use `-RequireCompletionComplete` for production publication. It rejects any
artifact set whose completion audit status is not `complete`, while
`-RequireCompletionAudit` only requires the audit to exist and validates the
release-gate evidence strength.

Add `-RequireSourceStateAudit` on production release builders. It rejects dirty
source trees and verifies the source-state JSON contains the expected fields.
When both hosted CI and source-state summaries are present, the artifact
verifier also rejects mismatched head SHAs.

Use `-RequireProductionReady` for the final publication preflight. It is a
single shorthand for the production-required checks: signed and trusted MSIX,
signing audit, completion audit status `complete`, IPC boundary audit, Linux
surface and behavior parity, hosted CI green run, and clean source-state audit.

Then refresh release notes so the published notes include the full evidence
summary:

```powershell
.\windows\write-release-notes.ps1
```

## Refreshing the portable directory

Windows locks a running `tmux.exe`, so the portable directory cannot be
overwritten while a client or server from that directory is running. Detach and
stop it first:

```text
Ctrl+b then d
```

```powershell
cd D:\Users\jonaszchen\Documents\tmux\dist\tmux-win32-portable
.\tmux.exe kill-server
```

If the directory is still in use, `windows/package-mingw.ps1` reports the
matching `tmux.exe` PIDs and repeats the detach/`kill-server` instruction before
copying files.

Then rebuild and repackage from the repository root:

```powershell
cd D:\Users\jonaszchen\Documents\tmux
.\windows\build-mingw.ps1 -Yacc C:\msys64\usr\bin\bison.exe
.\windows\package-mingw.ps1 `
  -Tmux .\tmux.exe `
  -Output .\dist\tmux-win32-portable `
  -ZipPath .\dist\tmux-win32-portable.zip `
  -Zip
```
