# Windows parity audit

Objective: build a native Windows tmux with the same practical feature surface
as the Unix/Linux build.

This file is the completion checklist. A green build or smoke is evidence, not
completion by itself; every row below needs either direct coverage or an
accepted reason why the Unix behavior does not apply on Windows.

## Gates

| Gate | Evidence | Status |
| --- | --- | --- |
| Native MinGW build | `windows/build-mingw.ps1` builds `tmux.exe` with bison and libevent. | Covered locally |
| Portable artifact | `windows/package-mingw.ps1 -Zip -RunSmoke` copies runtime DLLs, writes `manifest.json`, emits `.zip` and `.sha256`. | Covered locally |
| Release verifier | `windows/release-check.ps1` builds or reuses `tmux.exe`, packages, runs packaged smoke, verifies zip and manifest hashes, audits command/option/key-binding surface, can build an unsigned MSIX with `-BuildMsix`, verifies zip install/uninstall, can run targeted respawn stress, packaged smoke stress, mixed soak, console attach soak, clipboard contention stress, and optional visible Windows Terminal UI verification with `-RunVisualTerminalVerify`, and writes `dist/release-check.json` with the passed gate steps and artifact hashes. | Covered locally |
| Artifact verifier | `windows/verify-release-artifacts.ps1 -RequireMsix` cross-checks the zip sidecar, package manifest hashes, release summary, command-surface summary, MSIX hash summary, and MSIX signature state. | Covered locally |
| Release notes | `windows/write-release-notes.ps1` generates `dist/windows-release-notes.md` from verified JSON summaries. | Covered locally |
| Completion audit | `windows/completion-audit.ps1` reads the release summary, command-surface summary, MSIX summary, visible-terminal summary, optional signing audit, optional IPC boundary summary, optional Linux surface parity matrix, optional focused Linux behavior matrix, optional hosted CI audit, and optional source-state audit, then writes `dist/completion-audit.json` with a requirement-to-evidence checklist, covered evidence, and explicit non-completion items. | Reports not complete |
| Release policy | `windows/RELEASE.md` records the required local gate, required artifacts, signing expectations, hosted CI requirement, and do-not-publish conditions. | Documented |
| Hosted CI | `.github/workflows/windows-mingw.yml` runs the release check on `windows-latest` with respawn stress, IPC ACL/token stress, job stress, client lifecycle stress, signal matrix stress, clipboard contention stress, one packaged stress iteration, a short mixed soak, a short console attach soak with repeated reattach cycles, unsigned MSIX packaging, artifact verification, IPC boundary audit, release-note generation, completion-audit generation, an Actions step summary with hashes/counts/audit status, and uploads the portable zip, MSIX, release summary, command-surface summary, IPC boundary audit, completion audit, and release notes artifacts. `.github/workflows/windows-release.yml` is a manual release-candidate workflow that can create draft GitHub releases only. | Scaffolded, needs hosted run history |

Latest full local gate evidence, from 2026-05-18:

```powershell
.\windows\release-check.ps1 `
  -CC 'C:\msys64\mingw64\bin\gcc.exe' `
  -CXX 'C:\msys64\mingw64\bin\g++.exe' `
  -Yacc 'C:\msys64\usr\bin\bison.exe' `
  -RespawnIterations 20 `
  -IpcAclIterations 3 `
  -JobStressIterations 10 `
  -ClientStressIterations 5 `
  -SignalMatrixIterations 3 `
  -RunConfigStress `
  -StressIterations 1 `
  -SoakSeconds 10 -ConsoleSoakSeconds 10 `
  -ConsoleReattachCycles 2 `
  -ClipboardStressIterations 3 `
  -RunVisualTerminalVerify -BuildMsix -SmokeTimeoutSeconds 180
```

This passed packaged smoke, zip sidecar verification, manifest hash
verification, command-surface audit, unsigned MSIX packaging with `makeappx`,
targeted respawn stress, IPC endpoint ACL/token stress, job stdout/stderr and
background-process stress, zip install/uninstall verification, multi-client
lifecycle stress, signal matrix stress, config parser stress, one packaged
stress iteration, mixed runtime soak, console attach soak with repeated
reattach cycles plus raw Ctrl+C ETX delivery, and clipboard contention stress.
It also passed visible Windows
Terminal UI verification that opened a real terminal, attached a client, and
confirmed the marker output through UI Automation. This run followed a fix in
`osdep-windows.c` so `pane_current_command` ignores Windows console host helper
processes (`conhost.exe` and `OpenConsole.exe`) when selecting the active
ConPTY child; the same Ctrl+C path then passed both direct smoke and packaged
stress.

Follow-up artifact verification, extended evidence verification, and
release-note generation also passed:

```powershell
.\windows\verify-release-artifacts.ps1 -RequireMsix `
  -RequireSigningAudit `
  -RequireCompletionAudit `
  -RequireIpcBoundaryAudit `
  -RequireLinuxParity `
  -RequireLinuxBehaviorParity `
  -RequireHostedCiAudit
.\windows\write-release-notes.ps1 `
  -Output .\dist\windows-release-notes.md
.\windows\ipc-boundary-audit.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\ipc-boundary-audit.json `
  -RunSystemTaskProbe `
  -CreateTemporaryLocalUser `
  -RequireComplete
.\windows\linux-parity-matrix.ps1 `
  -WindowsTmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\linux-parity-matrix.json
.\windows\linux-behavior-parity.ps1 `
  -WindowsTmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\linux-behavior-parity.json
.\windows\source-state-audit.ps1 `
  -Output .\dist\source-state-audit.json
$headSha = (& git rev-parse HEAD).Trim()
.\windows\hosted-ci-audit.ps1 `
  -HeadSha $headSha `
  -Output .\dist\hosted-ci-audit.json
.\windows\signing-audit.ps1 `
  -Msix .\dist\tmux-win32.msix `
  -MsixSummary .\dist\tmux-win32.msix.json `
  -Output .\dist\signing-audit.json
.\windows\clipboard-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Iterations 3 -HoldMilliseconds 500 -TimeoutSeconds 60 `
  -RequireAvailable
```

Latest observed portable zip SHA256:
`1a98e4093efef9297a84f2f77f7c811ac838e593103d5a1576fa7e41970c3d36`.

Latest observed unsigned MSIX SHA256:
`ae172fd82ee275441462e23fe43e69851271ff0340009bcac59c1e239bea1e1c`.

Latest completion audit:

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
.\windows\verify-release-artifacts.ps1 -RequireMsix `
  -RequireSigningAudit `
  -RequireCompletionAudit `
  -RequireIpcBoundaryAudit `
  -RequireLinuxParity `
  -RequireLinuxBehaviorParity `
  -RequireHostedCiAudit `
  -RequireSourceStateAudit
```

This currently reports `status=not_complete` with two missing items:
production trusted signing and hosted CI green run evidence. Clean committed
source-state evidence is now covered by the source-state audit for the checked
out release-candidate tree. The current release summary now meets the documented
release-gate stress thresholds. Linux behavior parity is now covered by the
Linux surface matrix, 148 focused Windows/Linux behavior cases, and the behavior
category coverage matrix. The Windows IPC ACL/domain/service boundary checklist
is covered by the current local IPC boundary audit. The extended artifact
verifier now also requires hosted CI summaries to identify the target head SHA
and rejects a passed hosted CI summary whose green run has a different head SHA.
When source-state evidence is present too, the verifier also rejects a
hosted-CI head SHA that does not match the source-state head SHA.
With completion evidence required, the verifier also rejects release summaries
with missing, skipped, or failed required release steps, or below the documented
release-gate stress minimums, including clipboard stress.
Production publication can
add `-RequireCompletionComplete` so any remaining completion-audit gap blocks a
non-draft release, and `-RequireHostedCiGreen` so blocked or missing hosted CI
evidence fails independently.

The `windows/package-msix.ps1 -Sign` path has also been exercised with a
temporary self-signed code-signing certificate. The script now preflights that
the MSIX `Publisher` exactly matches the signing certificate subject before
calling `signtool.exe`; the test-signed MSIX carried an Authenticode signer but
reported an untrusted-certificate status, as expected for a disposable
self-signed certificate that was not installed as trusted.

Additional local runtime smoke evidence from 2026-05-15: after adding
near-`MAX_PATH` cwd coverage for panes and `run-shell -c`, rebuilding with
`C:\msys64\usr\bin\bison.exe`, and repackaging `dist\tmux-win32-portable`, this
passed:

```powershell
.\windows\smoke-runtime.ps1 -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -TimeoutSeconds 60
```

`windows/verify-portable.ps1` is also available as a short user-facing sanity
check for the portable package. It verifies `tmux -V`, detached session
creation, pane input/output, command clients, and `kill-server` without running
the full smoke suite.

`windows/diagnose-console.ps1` records terminal handle mode, default endpoint
state, and running tmux processes for user reports where a bare interactive
`tmux.exe` exits with `[lost tty]`; it can also reset the default endpoint and
run the quick portable verification.

Additional clipboard contention evidence from 2026-05-18:
`windows/clipboard-stress.ps1` passed three iterations against the current
portable package while an external process held the Windows clipboard open for
500ms before each `set-buffer -w` and `refresh-client -l` operation. This
directly exercises the native clipboard open retry path under transient
clipboard ownership. The current full local release-check gate now runs the
same coverage with `-ClipboardStressIterations 3` and records the result as a
`clipboard-stress` release step; clipboard availability is required so a
headless skip cannot be recorded as a pass.

Latest respawn-specific regression evidence from 2026-05-15: after bounding
ordinary ConPTY/process `CloseHandle` calls as well as `ClosePseudoConsole`,
`windows/respawn-stress.ps1` completed 20 consecutive `respawn-pane -k` cycles
against the packaged Windows binary, followed by full runtime smoke and the
full local release-check gate above.

Additional attached-console soak evidence from 2026-05-18:

```powershell
.\windows\console-attach-soak.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -DurationSeconds 120 -ReattachCycles 10 -TimeoutSeconds 120
```

This passed against the current portable package: 120 seconds of real-console
attach input, 60 resize events, ten reattach cycles, Ctrl+C/Ctrl+Break
interruption checks, and raw Ctrl+C ETX delivery.

Additional mixed runtime soak evidence from 2026-05-15:

```powershell
.\windows\soak-runtime.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -DurationSeconds 60
```

This passed 62.9 seconds and 108 mixed pane/job/resize/pipe iterations against
the current portable package.

Additional job-specific regression evidence from 2026-05-15:

```powershell
.\windows\job-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable-latest\tmux.exe `
  -Iterations 10 -BackgroundJobs 8 -TimeoutSeconds 60
```

This passed repeated `run-shell -E` mixed stdout/stderr output, concurrent
background `run-shell -b` jobs, and cleanup of a long background job when
`kill-server` tears down the server.

Additional client lifecycle evidence from 2026-05-15:

```powershell
.\windows\client-lifecycle-stress.ps1 `
  -Tmux .\dist\tmux-win32-portable-latest\tmux.exe `
  -Iterations 5 -CommandClients 8 -TimeoutSeconds 60
```

This passed concurrent command clients, repeated control-mode attach/command
detach, repeated redirected attached-client input/detach, and final server
cleanup.

Additional command-surface/default-option evidence from 2026-05-15:
`windows/audit-command-surface.ps1` passed against the releasecheck package
with 90 commands, 65 global options, 32 server options, 72 window options, 273
key bindings, required key tables, and Windows default option checks for
`default-shell`, `default-terminal`, `lock-command`, `set-clipboard`,
`exit-empty`, `mode-keys`, and `window-size`.

Additional config parser evidence from 2026-05-15:
`windows/config-parser-stress.ps1` passed against the releasecheck package,
covering semicolon-separated config commands, explicit nested `source-file`,
`%ENV%` source globs, paths with spaces and shell metacharacters, hooks,
`if-shell`, key bindings, and format-bearing option values.

Additional Linux surface parity evidence from 2026-05-18:
`windows/linux-parity-matrix.ps1` compared the current Windows portable binary
against WSL `tmux 3.6` and passed with zero Linux command/option/key-table
surface items missing on Windows. Windows reported `tmux next-3.7`, 90
commands, and 273 key bindings; Linux reported 90 commands and 267 key
bindings. The same matrix now compares common global, server, and window option
default values, reports zero unapproved default-option mismatches, and records
13 approved platform or version differences.

Additional focused Linux behavior parity evidence from 2026-05-18:
`windows/linux-behavior-parity.ps1` passed 148 matched Windows/Linux cases for
session lifecycle, `has-session` exit codes, session group sharing, `kill-session`, pane split count, resize-pane zoom toggling, select-window/last-window/select-pane/last-pane active state, next/previous-window navigation, window list shape, buffer round trip,
buffer append, buffer save/load through files, buffer save append, buffer list/delete, global, server, window, and user option
set/show plus unset/default behavior, environment set/show, environment unset, environment inheritance into panes, format
expansion, `new-window -c`, `run-shell -c`, and dynamic pane cwd selection, pane current command/path formats, `source-file` configuration
loading, key binding bind/list/unbind and key-note listing, `select-layout`,
`run-shell -C`, `run-shell -b`, `if-shell`, `list-commands`, `show-messages`, `pipe-pane -O/-I`, hook execution and hook listing, session rename,
window rename/link/unlink/move/kill/swap, `respawn-window`,
`break-pane`/`join-pane`, `respawn-pane`, `swap-pane`, `rotate-window`, `kill-pane`,
`resize-pane`,
`send-keys` plus capture, capture-pane history range, `paste-buffer` into a pane, copy-mode copy-line and
search navigation, copy-mode history search, copy-mode multi-line selection,
copy-mode rectangle selection, pane output capture, pane format lists,
control-mode command-client output, `wait-for` lock/unlock, and version
probing.
The same summary records `CategoryCoverage` for sessions, windows, panes,
buffers, options, environment, paths, formats, configuration, key-bindings,
commands, copy-mode, hooks, and control mode; all required categories are
covered on both Windows and Linux.

Additional hosted CI audit evidence from 2026-05-18:
the current local release-candidate head is not present on `origin`, and the
GitHub connector query for that head returned no workflow runs. The generated
`dist/hosted-ci-audit.json` records `status=no_run_for_head` for the target head
SHA. Hosted CI remains open until the release branch or commit is pushed and a
published workflow records a successful run for the same head SHA.

Additional source-state audit evidence from 2026-05-18:
`windows/source-state-audit.ps1` now records clean committed source-state
evidence for the checked-out release-candidate tree, with `dirty=False`, zero
tracked changes, zero untracked files, and the exact head SHA plus source-state
fingerprint in `dist/source-state-audit.json`. This closes the previous
source-state completion item; the audit must still be rerun after any source or
documentation commit before publishing artifacts.

Additional signing audit evidence from 2026-05-18:
`windows/signing-audit.ps1` checked the current MSIX and reported `unsigned`
with Authenticode status `NotSigned`. The same audit confirmed the MSIX summary
SHA256 matches the actual package and the summary Publisher matches the package
manifest Publisher (`CN=tmux`), so production trusted signing remains open with
direct package-level evidence rather than metadata ambiguity. The artifact
verifier now rejects signing metadata mismatches whenever signing-audit evidence
is required.

## Feature Checklist

| Area | Current evidence | Missing or weak coverage |
| --- | --- | --- |
| Server startup and IPC | Smoke covers detached server lifecycle, command clients, endpoint cleanup, default config search, and `kill-server`. `windows/verify-portable.ps1` covers a short portable-package startup path. `windows/client-lifecycle-stress.ps1` covers repeated concurrent command clients, control-mode clients, redirected attached clients, detach, and cleanup. Windows IPC uses loopback plus endpoint-token authentication, and Windows client startup removes a stale endpoint under the startup lock before launching a replacement server. | Longer endpoint upgrade/migration behavior and ACL edge cases. |
| Sessions, windows, panes | Smoke covers `new-session`, `new-window`, `split-window`, `swap-pane`, `link-window`, `unlink-window`, `break-pane`, `join-pane`, `respawn-pane`, `respawn-window`, `resize-pane`, and graceful cleanup of ConPTY windows. `windows/linux-behavior-parity.ps1` adds matched Windows/Linux session group sharing, `has-session` exit codes, `kill-session`, select-window/last-window/select-pane/last-pane active state, next/previous-window navigation, `swap-window`, `respawn-window`, `swap-pane`, `rotate-window`, `kill-pane`, `resize-pane`, and resize-pane zoom toggle cases. A targeted local loop covered 20 consecutive `respawn-pane -k` cycles after the bounded `CloseHandle` fix. Soak covers a short mixed pane/job/resize/pipe workload. | Longer soak durations and repeated layout churn beyond current stress/soak loops. |
| Pane PTY and process lifecycle | Smoke covers ConPTY pane I/O, `pane_current_command`, `pane_current_path`, cwd selection, a PowerShell `default-shell` pane and shell-command window, child process cleanup, active child interruption for cmd-hosted and PowerShell children, raw-input ETX handling, bounded pseudoconsole close, and bounded ConPTY/process handle close to avoid server-loop wedges during respawn and cleanup. `windows/linux-behavior-parity.ps1` adds matched Windows/Linux pane current command/path format checks and rejects Windows console-host helper names for the current command. | More shells and raw-mode applications; crash/teardown behavior under heavy output and rapid exits. |
| Attached clients | Smoke covers redirected ordinary attach, control-mode attach, control subscriptions, resize, flow control, popup/menu/choose/prompt/confirm on an attached client, a console-style attach hook probe, an `AllocConsole` real-console attach input/repeated-resize probe, real-console attached Ctrl+C for cmd-hosted and PowerShell children, real-console attached Ctrl+Break for cmd-hosted children, real-console attached raw Ctrl+C ETX delivery, and an optional longer console attach soak that repeats input, resize churn, attach/detach cycles, Ctrl+C/Ctrl+Break interruption, and raw Ctrl+C ETX delivery. `windows/visual-terminal-verify.ps1` opens a real Windows Terminal window and verifies visible attached-client output via UI Automation. `windows/client-lifecycle-stress.ps1` adds repeated redirected attach/detach and control-mode attach/detach. | Broader long-running interactive Windows console attach lifecycle beyond the current focused smoke, console soak, and UIA-visible attach check. |
| Commands and jobs | Smoke covers `run-shell` including quoted absolute targets with spaces and shell metacharacters, command-client stdout, and `-E` interleaved stdout/stderr, `run-shell -b`, `run-shell -c` including cwd paths with spaces and shell metacharacters plus local administrative-share UNC cwd for `cmd.exe` when available, `if-shell`, `pipe-pane` including a quoted absolute target with spaces and shell metacharacters plus a 160-line bulk-output case, `pipe-pane -I`, and `pipe-pane -IO`. `windows/job-stress.ps1` adds repeated `run-shell -E`, concurrent background `run-shell -b`, and background cleanup during `kill-server`. `windows/linux-behavior-parity.ps1` adds matched Windows/Linux `run-shell -b`, `run-shell -c` cwd selection, `pipe-pane -O` output capture, `pipe-pane -I` input injection, `list-commands` common entries, and `show-messages` command-log cases. | Richer shell command quoting matrix and longer job races beyond the current focused stress. |
| Copy mode and buffers | Smoke covers search navigation, copy-line, multi-line selection, rectangle selection, `paste-buffer`, `copy-pipe-line`, file transfer, buffer save/load, and a 64KB binary buffer round-trip. `windows/linux-behavior-parity.ps1` adds matched Windows/Linux buffer round-trip, buffer append, buffer save/load, buffer save append, buffer list/delete, copy-mode copy-line, search navigation, history search, multi-line selection, and rectangle selection cases. | Richer copy-mode history and selection edge cases. |
| Clipboard | Smoke covers native Windows clipboard set/get, `refresh-client -l`, and OSC 52 pane clipboard updates. Native clipboard open retries handle transient ownership. `windows/clipboard-stress.ps1` adds transient hostile-owner coverage for both `set-buffer -w` and `refresh-client -l`. | Clipboard format variants beyond text and longer hostile-owner timing. |
| Configuration, parser, formats, hooks | Smoke covers startup `-f`, `source-file`, Windows `%VAR%` and wildcard config expansion, hooks, parser Windows paths, and environment behavior. `windows/config-parser-stress.ps1` adds semicolon-separated config commands, explicit nested `source-file`, `%ENV%` source globs, paths with spaces and shell metacharacters, hooks, `if-shell`, key bindings, and format-bearing option values. `windows/audit-command-surface.ps1` checks command count, required commands, global/server/window options, key-binding count, required key tables, and Windows default option values. `windows/linux-parity-matrix.ps1` compares Linux tmux command/option/key-table surface plus common default option values against Windows and currently reports zero Linux surface items missing on Windows and zero unapproved default-option mismatches. `windows/linux-behavior-parity.ps1` adds 148 focused WSL/Windows behavior checks for core server state, session groups, `has-session` exit codes, `kill-session`, select-window/last-window/select-pane/last-pane active state, next/previous-window navigation, resize-pane zoom toggling, buffers, paths, formats, configuration, key-bindings, copy-mode, hooks, control mode, and command workflows, including matched global/server/window/user option set/show and option unset/default behavior, environment set/unset and pane inheritance, `source-file` configuration loading, key binding bind/list/unbind and note listing, hook listing, pane current command/path formats, dynamic pane cwd format updates, capture-pane history range, buffer append, buffer save append, and rotate-window pane reordering. | Broader upstream config corpus and behavior-level semantics for every option against upstream defaults. |
| Signals and terminal modes | Smoke covers pane `C-c` active-child interruption for cmd-hosted and PowerShell children, ETX forwarding for raw input, `send-keys C-Break` delivering a Windows `CTRL_BREAK_EVENT`, real-console attached Ctrl+C for cmd-hosted and PowerShell children, real-console attached Ctrl+Break for cmd-hosted children, and real-console attached raw Ctrl+C delivering ETX to a raw-input PowerShell probe. `windows/signal-matrix-stress.ps1` repeats pane-delivered `C-c`, controlled `C-Break`, cmd-hosted and PowerShell child interruption, interactive `choice.exe` interruption, and raw `C-c` ETX delivery. | Full raw-mode and shell-specific signal matrix across more native console apps and longer real attached-console runs. |
| Paths, cwd, environment | Smoke covers Windows cwd selection/fallback, cwd paths with spaces and shell metacharacters for panes, jobs, and popups, near-`MAX_PATH` cwd paths for panes and `run-shell -c`, junction cwd paths for panes and jobs, symlink cwd paths for panes and jobs when the host permits symlink creation, local `\\?\C:\...` cwd prefix normalization for panes and jobs, local administrative-share UNC cwd for `cmd.exe` panes and `run-shell -c` jobs when available, PowerShell `default-shell` command windows started in a local administrative-share UNC cwd when available, dynamic pane cwd, path expansion, default shell validation, and case-insensitive environment lookup. `windows/linux-behavior-parity.ps1` adds matched Windows/Linux `new-window -c`, `run-shell -c`, and dynamic pane cwd selection with directory names containing spaces. | Very long current directories beyond Windows `CreateProcess` cwd limits, symlink behavior on locked-down hosts, and richer permission failures. |
| Security and users | IPC endpoint has current-user DACL and token authentication; Windows owner uid/name fallback is used by ACL and formats. `windows/ipc-acl-stress.ps1` checks endpoint ACLs, endpoint file format, bad-token rejection over a raw TCP connection, and continued valid-client connectivity after a rejected token. `windows/ipc-boundary-audit.ps1 -RunSystemTaskProbe -CreateTemporaryLocalUser -RequireComplete` passed locally for endpoint owner/DACL checks, no broad or inherited allow ACEs, bad-token rejection, valid reconnect, SYSTEM scheduled-task endpoint-read denial, alternate temporary-user endpoint-read denial, and temporary user cleanup. | Broader domain-account policy variants and non-elevated host coverage beyond the current local temporary-user and SYSTEM probes. |
| Packaging and release | Portable zip, manifest hashes, DLL dependency discovery, user-level install/uninstall script, unsigned MSIX package generation with `makeappx`, tested MSIX signing code path with a temporary self-signed certificate, release-check gate with zip install verification, command-surface audit, respawn stress, IPC ACL/token stress, job stress, client lifecycle stress, signal matrix stress, config parser stress, signing audit, IPC boundary audit, Linux surface parity matrix, focused Linux behavior parity plus category coverage matrix, hosted CI audit, source-state audit, artifact verifier, release-note generator, JSON summaries, release checklist, CI artifact upload scaffold, manual draft-release workflow, and `Makefile.am` dist entries for Windows scripts/docs exist. | Production trusted-code-signing certificate, signed release artifacts, hosted CI history, and upstream release approval/merge. |

## Current Non-Completion Items

- Long-running interactive Windows console attach lifecycle is improved by the
  optional console attach soak with input, resize churn, repeated attach/detach
  cycles, Ctrl+C/Ctrl+Break interruption, and raw Ctrl+C ETX delivery. A
  120-second local run with ten reattach cycles has passed, but this is still
  not complete beyond focused local runs.
- Full signal parity is not complete beyond the current pane `C-c`, ETX,
  controlled `C-Break`, interactive `choice.exe`, real-console attached
  Ctrl+C, cmd-hosted real-console Ctrl+Break, and real-console attached raw
  Ctrl+C smoke coverage.
- Very long local drive cwd paths beyond Windows process cwd limits are not
  Linux-equivalent. Current pane/job code treats those start directories as
  unsupported and falls back instead of failing the spawn; near-`MAX_PATH`
  directories below that limit are covered by smoke.
- Hosted CI has a workflow file but no observed green hosted run in this
  workspace.
- IPC service-boundary coverage now includes local SYSTEM scheduled-task
  endpoint-read denial and a temporary local second-user endpoint-read denial.
  Broader domain-account policy variants still need hosted or lab coverage.
- Production trusted-code-signing certificate, signed release artifacts, and
  release publishing remain open.
