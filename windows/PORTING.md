# Native Windows porting status

For registering this Windows build with TmuxHub, see [TMUXHUB.md](TMUXHUB.md) and `windows/install.ps1`.

This tree is still primarily a POSIX tmux implementation. The native Windows
port must preserve the Linux feature model: server/client separation, sessions,
windows, panes, jobs, copy mode, control mode, configuration, formats, hooks,
and terminal rendering.

## Active Windows IPC path

Current active server/client IPC path: **old-loopback-token** (`compat/win32-ipc.c`).

The following files are active:
- `client.c`: `win32_ipc_connect()` for client connections
- `server.c`: `win32_ipc_listen()` + `win32_ipc_accept_nonblocking()` for server listener
- `compat/win32-ipc.c`: loopback TCP listener, endpoint file, token authentication

The following files are **experimental / not active** in the runtime path:
- `compat/win32-daemon.c`: named pipe control channel prototype (`win32_daemon_spawn_server()`)
- `compat/win32-endpoint.c`: atomic endpoint record with SID/pid/pipe fields

The named-pipe daemon endpoint prototype exists, but the active client/server path
still uses `win32-ipc.c` loopback token IPC until the active-path audit is green.
`proc.c` contains a stub call to `win32_daemon_spawn_server()` that returns `ENOSYS`.

## Current foundation

- `configure.ac` now recognizes MinGW/native Windows hosts as `PLATFORM=windows`.
- `osdep-windows.c` provides the platform hook required by the existing
  `osdep-@PLATFORM@.c` build pattern.
- `compat/win32-socketpair.[ch]` creates a Winsock loopback socket pair for
  event-loop friendly local byte streams and provides socket helpers for
  nonblocking mode, pending-byte checks, shutdown, and close.
- `compat/win32-ipc.[ch]` is the first native replacement building block for
  Unix-domain server sockets: it creates loopback TCP listeners, writes a small
  endpoint file, and authenticates local connects with a per-listener random
  token before returning a byte-stream socket.
- The Windows IPC endpoint file is created with a DACL for the current user and
  is recreated with `CREATE_NEW` after deleting any stale endpoint file, so the
  loopback authentication token is not left readable through inherited
  temporary-directory permissions.
- `compat/imsg.[ch]` and `compat/imsg-buffer.c` now have an initial Windows
  byte-stream path for Winsock sockets without fd passing. This is enough for
  ordinary imsg payloads over the new IPC helper; stdin/stdout handle transfer
  is handled with explicit Windows handle duplication messages.
- `client.c`, `server.c`, `server-client.c`, and `proc.c` now have the first
  `_WIN32` IPC integration points: client connects use the endpoint helper,
  server listen/accept uses the endpoint listener, peers store socket-sized
  `imsg_fd_t` handles, and Windows peer cleanup closes Winsock sockets.
- Windows IPC startup now initializes Winsock before libevent is initialized,
  so the win32 backend can create its internal signal/socket helpers without
  tripping over a missing `WSAStartup()`.
- Accepted Windows IPC sockets temporarily switch to blocking mode while the
  endpoint token is read, with a short receive timeout, then return to
  nonblocking mode for libevent/imsg. Client IPC sockets also switch to
  nonblocking mode after the endpoint token is sent.
- `proc.c` has a Windows peer timer that uses `FIONREAD` plus the imsg output
  queue length to cover readiness notifications missed by the libevent win32
  backend. Windows peers also flush queued imsg output at the end of a read
  callback, which is needed for replies queued while dispatching another imsg.
- `tmux.c` now has initial Windows startup branches for default shell
  discovery (`ComSpec`/`cmd.exe`), default endpoint path creation under the
  user's local app data or temp directory, current directory/home discovery,
  timer reads, and skipping the Unix pty master bootstrap. A failed Windows
  client connect can enter the existing `CLIENT_NOFORK` server path, so the
  foreground `tmux -D` shape has a native entry point.
- `compat/win32-fnmatch.c` provides the POSIX `fnmatch()` glob matcher that
  MinGW lacks, including the wildcard, character-class, path-separator, period,
  escape, and case-folding behavior used by tmux matching paths.
- `compat/win32-handle.[ch]` provides the first Windows replacement for imsg
  fd passing: the client serializes a source process id plus HANDLE value, and
  the server duplicates it into its own process with `DuplicateHandle()` before
  converting it to a CRT fd.
- `compat.h` and `tmux.h` have the first Windows header guards for missing
  Unix `ioctl`, `uio`, `termios`, and `fnmatch` headers, with placeholder
  termios/winsize definitions so shared structures can be parsed. This is a
  syntax bridge only; real terminal mode behavior still belongs in the Windows
  console layer.
- `client.c` and `server-client.c` now use the Windows handle-transfer payload
  for `MSG_IDENTIFY_STDIN` and `MSG_IDENTIFY_STDOUT` instead of relying on
  SCM_RIGHTS. This establishes cross-process handle ownership for client stdio.
- `compat/win32-stdio.[ch]`, `server-client.c`, and `control.c` bridge
  duplicated client stdin/stdout handles into Winsock socket pairs so control
  mode can keep using libevent bufferevents on Windows. The close path cancels
  blocking synchronous I/O before releasing the bridge.
- `server-client.c` and `tty.c` also use the stdio socket bridge for ordinary
  terminal clients on Windows. The server-side tty path now reads and writes
  the bridge sockets, skips POSIX termios mutation, and queries the duplicated
  output handle for console size when that handle is a real console.
- Real Windows console stdin is now proxied from the client process instead of
  being read directly by the server. Console `MSG_IDENTIFY_STDIN` payloads are
  marked as console handles, the server keeps the tty socket bridge open
  without a server-side console reader, and the attached client forwards
  console input bytes through `MSG_STDIN` into that bridge.
- The stdio bridge can save, adjust, and restore Windows console modes for
  terminal clients: ordinary attaches and `-CC` control clients enable raw-ish
  input plus VT output where the duplicated handles are real consoles, while
  pipe clients safely fall back without terminal-mode changes.
- The stdio bridge also saves and temporarily switches the attached console
  input/output code pages to UTF-8 while tmux owns the terminal, then restores
  them on close.
- `client.c` has Windows guards around the POSIX termios/signal attach paths
  and now runs a simple console resize watcher that sends the existing
  `MSG_RESIZE` with a Windows size payload when the local stdout console window
  changes size. The server applies that payload directly because duplicated
  console handles are not reliable enough for server-side size queries.
- Windows clients now load the built-in `tmux-win32` terminal capability list
  even when stdin is redirected, so ordinary attached clients that use the
  Windows stdio bridge still provide required capabilities such as `clear` and
  `cup` to the server.
- `cmd-new-session.c` no longer tries to capture POSIX termios settings on
  Windows when creating a new attached session; Windows console mode setup is
  owned by the stdio bridge.
- `cmd-kill-server.c` can now shut down the server on Windows without POSIX
  signals by calling the shared `server_shutdown()` path directly.
- Windows IPC peers now map to a local owner uid after endpoint-token
  authentication, so `server_acl_join()` no longer rejects every native Windows
  client as uid `-1`. `server-access` has a first Windows path that recognizes
  the current Windows user (or `.`) without Unix `pwd` lookups; broader
  multi-user SID-based ACLs still need a later design.
- `format.c` and `cmd-queue.c` use the same Windows owner uid/name fallback for
  `#{uid}`, `#{user}`, `#{client_uid}`, `#{client_user}`, and command log
  attribution instead of calling Unix `getuid()`/`getpwuid()`.
- `compat/regex.h`, `compat/win32-regex.h`, and `compat/win32-regex.cc`
  provide a first Windows POSIX-regex shim backed by C++ `std::regex`, so
  format matching, substitutions, copy-mode search, and window matching can
  keep using `regcomp()`/`regexec()` style calls when native `regex.h` is
  missing.
- `run-shell` and `if-shell` now avoid direct `sys/wait.h` includes on Windows
  and use the Windows wait-status macros in `compat.h`, matching the
  POSIX-style status values returned by the Win32 job/process helpers.
- `compat/win32-conpty.[ch]` dynamically loads the Windows ConPTY API and
  spawns a process attached to a pseudoconsole with an optional explicit
  Unicode environment block.
- ConPTY process startup sets `STARTF_USESTDHANDLES` with empty standard
  handles before applying `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`, so shells
  write to the pseudoconsole pipe instead of inheriting the parent client's
  stdin/stdout when tmux is run from redirected or non-console handles.
- `compat/win32-pty.[ch]` bridges ConPTY pipe handles to a socket endpoint.
  This is the pty-like primitive tmux can use once the process creation paths
  are split for Windows.
- `compat/win32-command.[ch]` converts tmux UTF-8 command arguments to Windows
  wide strings and builds `CreateProcessW` command lines with Windows CRT
  quoting rules.
- `compat/win32-environment.[ch]` converts UTF-8 `NAME=VALUE` entries to a
  sorted Unicode environment block for `CreateProcessW`.
- `compat/win32-spawn.[ch]` is the high-level native pty spawn entry point:
  it accepts UTF-8 argv/cwd/environment inputs and calls either the
  ConPTY-backed pty layer or the redirected-process layer.
- `compat/win32-process.[ch]` starts non-PTY child processes with redirected
  stdin/stdout/stderr, bridges them to a socket, and exposes wait/terminate
  lifecycle hooks for background jobs.
- The Win32 pty layer now exposes process lifecycle hooks for exit polling,
  waiting, termination, resize, and cleanup; these are needed by tmux pane and
  job state handling.
- The Win32 pty close path waits briefly for a terminated ConPTY child before
  closing the pseudoconsole. If the process does not exit promptly, it skips
  the blocking pseudoconsole close and relies on process teardown cleanup.
  `ClosePseudoConsole` and ordinary ConPTY/process handle closes are run
  through bounded helper threads so pane destruction, `respawn-pane -k`,
  `kill-session`, `kill-server`, and background job cleanup cannot wedge the
  tmux server event loop if the Windows console host or a hooked `CloseHandle`
  call blocks while closing a pseudoconsole, pipe, process, thread, or job
  handle.
- ConPTY processes are started suspended, assigned to a Windows Job Object, and
  then resumed, so terminating or closing a pane/job can clean up child process
  trees rather than only the top-level shell. `win32_pty_terminate()` also uses
  a Toolhelp process-tree pass to catch shell-launched children.
- `job.c` has an initial `_WIN32` `JOB_PTY` path for jobs that stay owned by
  the job subsystem: it can spawn via ConPTY, resize, shut down writes, collect
  an exit status, and clean up through the Win32 pty lifecycle helpers.
- `job.c` also has an initial `_WIN32` non-PTY path for background jobs such as
  `run-shell` and `if-shell`: it uses the redirected-process helper instead of
  POSIX `fork()` plus `socketpair()`, preserves `JOB_SHOWSTDERR`, and reports a
  POSIX-style exit status to existing callbacks.
- Windows jobs now have a short poll timer for redirected-process and ConPTY
  jobs. It activates pending socket reads and continues tracking `JOB_CLOSED`
  jobs until the Windows process handle reports exit, so synchronous
  `run-shell` and `if-shell` commands resume their command queue items instead
  of waiting forever after the child process exits.
- `cmd-pipe-pane.c` has an initial `_WIN32` path for `pipe-pane`: it starts the
  pipe command with the redirected-process helper, attaches the process to a
  socket-backed bufferevent, and cleans up the Windows process/socket when the
  pipe is closed or the pane is destroyed.
- The redirected-process helper now treats the socket as a real duplex stream:
  closing the tmux-to-child direction closes only child stdin, while child
  stdout EOF only shuts down the socket send side. This is required for
  `pipe-pane -IO` style commands that write input and then read output.
- `window.c`, `server-fn.c`, and `popup.c` now have initial Windows pty
  ownership support for panes: a `JOB_PTY` popup can transfer its ConPTY/socket
  to a `window_pane`, and pane resize, event setup, exit-status capture, and
  destruction use the Win32 pty helpers.
- Windows ConPTY panes are now treated as live panes even though the POSIX
  `fd` stays `-1`: pane input checks use the Win32 socket, pane buffers are
  drained and reenabled in the server-client loop, and ConPTY pane writes flush
  the bufferevent output buffer directly to cover missed libevent win32 write
  readiness.
- `spawn.c` has an initial native Windows pane spawn path: after tmux has
  resolved pane argv/cwd/environment and size, it creates a ConPTY-backed pane
  directly instead of using `forkpty()`.
- The remaining POSIX child setup in `spawn.c` is now kept out of `_WIN32`
  builds, including the `forkpty()`, `tcgetattr()`/`tcsetattr()`, `exec*()`,
  signal cleanup, and utempter notification path. Windows pane creation reaches
  the ConPTY spawn path and then rejoins the shared pane completion logic.
- `compat.h` now neutralizes MinGW's CRT `environ` macro before tmux declares
  its own `struct environ`, exposes `TMUX_ENVIRON` for the few places that need
  the real process environment, and defines Windows `uid_t`/`gid_t` stand-ins.
  `environ.c` also avoids a direct `<fnmatch.h>` include on Windows so it uses
  the compat glob matcher.
- `tty-term.c` now has a first native Windows terminal capability fallback:
  clients without `TERM` identify as `tmux-win32`, MinGW no longer needs
  `<term.h>` for this file, and the fallback advertises common VT sequences for
  cursor movement, clearing, attributes, 256-colour, RGB colour, and common key
  sequences. It also advertises mouse and OSC 52 clipboard capabilities using
  the native Windows formatter syntax.
- Native Windows builds now also use `tmux-win32` as the default pane
  `TERM`, so programs running inside ConPTY panes see capabilities that match
  tmux's Windows fallback table instead of the Unix `screen` default.
- The Windows terminal capability formatter now expands both the native
  fallback `%d`/`%s` forms and the common terminfo forms used by tmux
  `terminal-features` (`%p1%d`, `%p2%s`, `%i`, and conditionals). It also uses
  a dynamically sized output buffer so large OSC 52 clipboard payloads are not
  truncated by the formatter itself.
- More root sources now build to temporary objects under a MinGW `_WIN32`
  compile probe with temporary libevent stubs: server, server-client,
  server-fn, proc, job, popup, tty, tty-keys, input, file, and the command
  sources no longer pull in missing POSIX-only headers. Optional sixel sources
  also compile when `ENABLE_SIXEL` is defined.
- `cmd-source-file.c` has a Windows glob fallback for `source-file` patterns,
  including Windows absolute paths and wildcard directory components, so native
  builds do not depend on `<glob.h>` just to expand configuration file
  arguments. Direct `source-file` command arguments on Windows now also expand
  leading `%VAR%` path components before globbing.
- Default Windows client startup can now try to launch a detached background
  server process when no endpoint is reachable: the client starts the current
  executable as `tmux -D -S <endpoint>` and retries the native IPC connection.
  A per-endpoint local Windows mutex serializes competing client startups.
  This is the first replacement for the POSIX `fork()`/`daemon()` startup
  shape; foreground `-D` still uses the in-process server path.
- While holding the startup mutex, Windows clients now remove a stale endpoint
  file before launching the replacement background server. This handles the
  common user-facing case where `default.endpoint` exists but the old listener
  is gone or no longer accepts the endpoint token.
- That detached Windows server startup now preserves user-specified startup
  context that would normally be inherited across `fork()`: explicit `-f`
  configuration files and the current `-v` log level are forwarded to the
  background `tmux -D -S <endpoint>` process.
- `compat/win32-time.c` provides the POSIX-style `localtime_r`, `gmtime_r`,
  and `ctime_r` helpers expected by the shared format and clock code, and the
  server client terminal-open path now avoids Unix `ttyname()` checks in native
  Windows builds.
- The shared UTF-8 code no longer stores decoded codepoints in Windows
  `wchar_t`; native builds use a 32-bit `utf8_wchar` codepoint type and manual
  UTF-8 encode/decode paths so supplementary-plane characters are not truncated
  by MinGW's 16-bit wide character type. When utf8proc is not enabled, Windows
  builds use an internal width fallback instead of the missing CRT `wcwidth()`.
- A refreshed MinGW object probe now compiles the native Windows root source
  set and Win32 compat helper set with temporary libevent headers, including
  the UTF-8, key, format, clock, IPC, stdio, ConPTY, process, spawn, regex, and
  time compatibility paths.
- `configure.ac` now keeps native Windows builds from pulling unused POSIX
  daemon/forkpty/closefrom/getdtable/getpeereid compatibility objects into the
  build. The generic compat objects needed by the Windows link path now compile
  under MinGW, including base64, htonll/ntohll, and recallocarray.
- The `cmd-parse.y` token formerly named `ERROR` is now `PARSE_ERROR`, avoiding
  a generated-header collision with the Windows SDK `ERROR` macro while keeping
  the parser grammar unchanged.
- `windows/probe-mingw.ps1` captures that probe as a repeatable smoke test. It
  compiles the Windows root source set plus compat objects, links
  `tmux-probe.exe`, runs `tmux-probe.exe -V` to cover the minimal startup path,
  and also builds and runs `terminal-format-probe.exe` to exercise the Windows
  terminal capability formatter for native `%d`/`%s`, terminfo `%p`
  parameters, `%i`, conditionals, string-length conditionals, RGB colour, and
  large OSC 52 payloads. The temporary config header only supplies
  `TMUX_VERSION`, so the probe now exercises the real Windows defaults for
  config files, lock command, and default terminal. It removes temporary outputs
  by default. The probe can build with parser and libevent stubs, or it can use
  a real bison-generated parser and libevent with
  `-UseGeneratedParser -UseSystemLibevent`. In system-libevent mode it accepts
  explicit `-LibeventCflags`/`-LibeventLibs`, asks `pkg-config` when available,
  and otherwise infers an MSYS2 libevent prefix from the compiler path,
  environment, `PATH`, or common MSYS2 install roots. The probe also adds the
  inferred MSYS2 `bin` directory to its process `PATH` so linked test
  executables can find the libevent DLL. That combined path has been validated
  with temporary MSYS2 bison/m4 packages, a CMake-built static libevent, and
  the local MSYS2 MinGW64 libevent package without explicit libevent flags; the
  probe keeps libevent libraries before the Windows system libraries so static
  Winsock references resolve correctly.
  Real MinGW/libevent builds skip the local `clock_gettime` fallback when the
  target CRT already provides it, and tmux's event callbacks now use
  `evutil_socket_t` so Win64 libevent headers compile without pointer-type
  diagnostic suppression. The probe can also export the linked executable with
  `-OutputExe` and stamp a caller-supplied version with `-Version`.
- `windows/build-mingw.ps1` is a thin native build entry point around that
  validated path. It requires a real bison/yacc and libevent, parses the tmux
  version from `configure.ac` when `-Version` is not supplied, auto-discovers
  MSYS2 libevent for the common local toolchain layout, and writes `tmux.exe`
  by default.
- `windows/package-mingw.ps1` builds a first portable distribution directory
  from an exported `tmux.exe`: it recursively reads PE import tables with
  `objdump`, skips Windows system/API-set DLLs, copies the MinGW/libevent
  runtime DLLs beside `tmux.exe`, writes a `manifest.json` with file sizes and
  SHA256 hashes, can emit a `.zip` plus `.sha256` sidecar with `-Zip`, and can
  run the runtime smoke against the packaged executable with `-RunSmoke`
  using a configurable smoke command timeout. It also preflights the output
  directory for running `tmux.exe` processes and reports the matching PIDs plus
  detach/`kill-server` guidance before attempting to overwrite a locked
  portable package.
- `windows/package-msix.ps1` can wrap the portable package into an unsigned
  MSIX using Windows SDK `makeappx.exe`, with a full-trust desktop manifest,
  console `tmux.exe` app execution alias, generated PNG assets, SHA256 summary,
  and optional `signtool.exe` signing when a certificate is supplied. MSIX
  signing requires `-Publisher` to match the signing certificate subject, and
  the script checks the certificate private key, validity window, and Code
  Signing EKU before invoking `signtool.exe`.
- `windows/release-check.ps1` is the first local release gate: it can build
  `tmux.exe`, create the portable zip, run the packaged runtime smoke, verify
  the zip `.sha256` sidecar, rehash every file recorded in the manifest, run
  the command-surface audit, optionally build the unsigned MSIX with
  `-BuildMsix`, and write `dist/release-check.json` with the passed gate steps
  and artifact hashes.
  It can also run targeted `respawn-pane -k` regression loops by passing
  `-RespawnIterations`, IPC endpoint ACL/token stress by passing
  `-IpcAclIterations`, `run-shell` stdout/stderr and background job stress by
  passing `-JobStressIterations`, multi-client lifecycle stress by passing
  `-ClientStressIterations`, signal matrix stress by passing
  `-SignalMatrixIterations`, config parser/source-file stress by passing
  `-RunConfigStress`, repeated packaged smokes by passing `-StressIterations`,
  a mixed workload soak by passing `-SoakSeconds`, a real-console attach soak
  by passing `-ConsoleSoakSeconds`, clipboard contention stress by passing
  `-ClipboardStressIterations`, and a visible Windows Terminal UIA check by
  passing `-RunVisualTerminalVerify`; its default smoke command timeout is
  higher than the standalone smoke default to reduce slow-host release-gate
  flakes.
- `.github/workflows/windows-mingw.yml` is an initial hosted CI entry point for
  the native Windows port. It installs MSYS2 MinGW64 GCC, bison, pkgconf, and
  libevent on `windows-latest`, runs `windows/release-check.ps1` with one
  packaged stress iteration, targeted respawn stress, IPC ACL/token stress,
  job stress, multi-client lifecycle stress, signal matrix stress, config
  parser stress, clipboard contention stress, a short mixed soak, a short
  console attach soak, two repeated real-console reattach cycles, unsigned
  MSIX packaging, IPC boundary audit, and writes release notes plus a
  completion audit and an Actions step summary before uploading the portable
  Windows zip, MSIX, release-check summary, IPC boundary audit, completion
  audit, and release notes artifacts.
- `.github/workflows/windows-release.yml` is a manual release-candidate
  workflow for a tag or ref. It runs the same Windows release gate, verifies
  artifacts, writes release notes, uploads artifacts, and can create only draft
  GitHub releases until production signing is configured.
- `windows/smoke-runtime.ps1` is the first repeatable runtime smoke for an
  exported native `tmux.exe`. It uses a unique `-L` endpoint, verifies the
  command client/server path, and cleans up its endpoint, temporary files, and
  any tmux processes matching that smoke endpoint.
- `windows/verify-portable.ps1` is a short user-facing sanity check for the
  portable package. It verifies `tmux -V`, detached session creation, pane
  input/output, command clients, and `kill-server` without running the full
  smoke suite.
- `windows/visual-terminal-verify.ps1` is the desktop-visible attached-client
  verifier. It opens a real Windows Terminal window, attaches a tmux client,
  sends pane input whose output contains a unique marker, and uses UI
  Automation to verify the marker is visible in the terminal. It can also save
  a PNG of the terminal window with `-ScreenshotPath`.
- `windows/VERIFY.md` is the user-facing verification guide. It covers the
  quick portable check, graphical attach check, bare interactive attach
  behavior, `Ctrl+b` then `d` detach, explicit PowerShell pane testing, console
  diagnostics, runtime smoke, respawn stress, job stress, client lifecycle
  stress, config parser stress, release-check, and the required `kill-server`
  step before overwriting a portable directory on Windows.
- `windows/diagnose-console.ps1` records the current terminal's Windows handle
  type, `GetConsoleMode` state, terminal-related environment variables,
  default endpoint status, running `tmux.exe` processes, and default-server
  session/client/pane state. If attached clients are present, it reports that a
  bare `tmux.exe` is expected to take over the terminal until `Ctrl+b` then
  `d` detaches it. With `-ResetDefault`, it also kills the default server and
  removes `default.endpoint`; with `-RunQuickVerify`, it chains the portable
  quick verification. This is intended for user reports such as a bare
  `tmux.exe` returning `[lost tty]` or appearing stuck after it has actually
  attached. The Windows client also reports the stdin proxy exit reason in the
  lost-tty message and ignores zero-byte console input events from
  resize/focus-style records instead of treating them as stdin EOF.
- `windows/stress-runtime.ps1` wraps `smoke-runtime.ps1` for repeated local or
  CI runs, reporting per-iteration timings and failing on the first smoke
  failure. This is intended for the longer-running regression coverage that is
  still needed beyond the default smoke.
- `windows/respawn-stress.ps1` repeatedly creates a fresh server and exercises
  `respawn-pane -k` against a ConPTY `cmd.exe` pane, then verifies the pane is
  usable after restart. It is aimed at regressions where ConPTY or process
  handle cleanup wedges the server during respawn.
- `windows/job-stress.ps1` repeatedly exercises `run-shell -E` mixed
  stdout/stderr output, concurrent background `run-shell -b` jobs, and cleanup
  of a long background job when `kill-server` tears down the server. `release-check`
  can include it with `-JobStressIterations`.
- `windows/ipc-acl-stress.ps1` repeatedly verifies the Windows IPC endpoint
  ACL, endpoint file format, bad-token rejection, and continued valid-client
  connectivity after a rejected raw TCP token attempt. `release-check` can
  include it with `-IpcAclIterations`.
- `windows/ipc-boundary-audit.ps1` writes a JSON endpoint-boundary audit for
  current-user owner/DACL state, broad-group and inherited ACE rejection,
  endpoint format, bad-token rejection, valid reconnect, optional alternate
  local/domain user endpoint-read denial, optional temporary local user
  creation/deletion for that probe, and optional SYSTEM scheduled-task
  endpoint-read denial.
- `windows/client-lifecycle-stress.ps1` repeatedly exercises concurrent
  command clients, a control-mode client, a redirected attached client,
  detach, and server cleanup. `release-check` can include it with
  `-ClientStressIterations`.
- `windows/signal-matrix-stress.ps1` repeatedly exercises Windows pane signal
  delivery for `C-c`, controlled `C-Break`, cmd-hosted and PowerShell
  children, and raw `C-c` ETX input. `release-check` can include it with
  `-SignalMatrixIterations`.
- `windows/config-parser-stress.ps1` exercises semicolon-separated config
  commands, explicit nested `source-file`, `%ENV%` source globs, paths with
  spaces and shell metacharacters, hooks, `if-shell`, key bindings, and
  format-bearing option values. `release-check` can include it with
  `-RunConfigStress`.
- `windows/linux-parity-matrix.ps1` uses WSL when available to compare a Linux
  `tmux` command/option/key-table surface against the Windows binary and emits
  `dist/linux-parity-matrix.json`. It is a surface matrix and does not replace
  behavior-level parity testing.
- `windows/linux-behavior-parity.ps1` uses WSL when available to run a focused
  behavior matrix on both Windows and Linux tmux for sessions, windows, panes,
  buffers, options, environment, format expansion, copy-mode copy/search/history
  plus multi-line and rectangle selection, `run-shell -C`, `pipe-pane -O/-I`,
  and `wait-for`.
- `windows/hosted-ci-audit.ps1` queries the GitHub Actions API for the origin
  repository and writes whether the expected Windows workflow exists and has a
  successful hosted run, optionally scoped to a release commit with `-HeadSha`.
- `windows/source-state-audit.ps1` records the current git head, branch, dirty
  state, tracked-change count, untracked-file count, tracked-diff hash,
  untracked-file hashes, and a combined source-state fingerprint so release
  artifacts can be tied to a clean committed source tree or compared against an
  exact dirty development state.
- `windows/signing-audit.ps1` records the MSIX Authenticode state, signer
  certificate details when present, whether the package is trusted, whether the
  MSIX summary hash matches the actual package, and whether package Publisher
  metadata matches the signer subject when a signer is present. The artifact
  verifier rejects signing metadata mismatches when signing-audit evidence is
  required.
- `windows/soak-runtime.ps1` runs a mixed pane/job/resize/pipe workload for a
  configurable duration against either the built binary or packaged portable
  `tmux.exe`.
- `windows/console-attach-soak.ps1` runs a longer `AllocConsole` attach
  workload with repeated console resizes, input markers, repeated attach/detach
  cycles, and real-console Ctrl+C/Ctrl+Break interruption, then verifies
  real-console raw Ctrl+C ETX delivery in the same session. `release-check`
  can include it with `-ConsoleSoakSeconds` and tune reattach churn with
  `-ConsoleReattachCycles` without making it part of the default local gate.
- `windows/audit-command-surface.ps1` checks the exported Windows binary's
  command list, global/server/window option lists, default key binding count,
  required key tables, and Windows-specific default option values such as
  `default-terminal`, `default-shell`, `lock-command`, `set-clipboard`, and
  `exit-empty` against a conservative tmux command-surface baseline.
  `release-check` runs it by default and writes `dist/command-surface.json`.
- `windows/verify-release-artifacts.ps1` verifies an already-produced `dist`
  directory by cross-checking the zip sidecar, portable manifest hashes,
  release summary, command-surface summary, MSIX hash summary, and MSIX
  signature state. It can also require and validate signing-audit,
  completion-audit, IPC boundary, Linux surface parity, Linux behavior parity,
  hosted CI audit, and source-state audit JSON summaries, including the target
  head SHA when hosted CI evidence is required, explicit hosted-green
  enforcement when requested, and clean-source enforcement when source-state
  evidence is required. When both summaries exist, it also
  rejects mismatched hosted-CI and source-state head SHAs. When completion
  evidence is required, it enforces the release-gate stress minimums documented
  in `windows/RELEASE.md`; with `-RequireCompletionComplete`, it also rejects
  artifact sets whose completion audit still has open gaps.
- `windows/write-release-notes.ps1` turns the verified JSON summaries into
  `dist/windows-release-notes.md` with artifact hashes, signing state, release
  gate command, and command-surface counts.
- `windows/completion-audit.ps1` reads the release summaries, visible terminal
  result, optional signing audit, optional IPC boundary audit, optional Linux
  surface parity matrix, optional focused Linux behavior parity matrix, and
  optional hosted CI audit, optional source-state audit, then writes
  `dist/completion-audit.json` with a requirement-to-evidence checklist,
  covered evidence, and the explicit remaining non-completion items. Hosted
  workflows pass the checked-out head SHA and their GitHub Actions run URL so
  the audit can record hosted CI evidence only when the observed green run
  matches the release commit.
- `windows/RELEASE.md` is the Windows release checklist: it records the
  required release-check command, required artifacts, signing expectations,
  hosted CI requirement, and conditions that block publication.
- `windows/PARITY-AUDIT.md` is the current prompt-to-artifact checklist for
  completion: each Windows parity area maps to concrete local evidence and the
  remaining weak or missing coverage.
- `windows/linux-behavior-parity.ps1` now compares 140 focused tmux behaviors
  between the native Windows binary and WSL/Linux, including session/window
  mutations, `has-session` exit codes, session group sharing, `kill-session`, `swap-window`, `respawn-window`, `swap-pane`, `rotate-window`, `kill-pane`, pane
  resize and zoom toggling, select-window/last-window/select-pane/last-pane active state, next/previous-window navigation, `new-window -c`, `run-shell -c`, and dynamic pane cwd selection, buffer append, buffer file round trips, buffer save append, buffer list/delete, global, server, window, and user option set/show plus unset/default behavior, formats
  including pane current command/path, copy-mode copy/search/history plus
  multi-line and rectangle selection, hooks,
  control mode command clients, `source-file` configuration loading, key binding
  bind/list/unbind, environment set/unset and inheritance into panes, pane input/capture including history range,
  paste-buffer, `run-shell -b`, `pipe-pane -O/-I`, and wait locks. Its JSON summary
  also records required behavior category coverage for sessions, windows,
  panes, buffers, options, environment, paths, formats, configuration,
  key-bindings, commands, copy-mode, hooks, and control mode.
- With the generated parser and a real static libevent, the exported
  `tmux.exe` now passes a first detached-server command smoke on Windows:
  `new-session -d -s smoke`, `list-sessions`, and `kill-server` complete over
  the native loopback IPC path.
- The same build now passes a detached pane I/O smoke on Windows:
  `new-session -d`, `send-keys "echo TMUX_WIN32_PANE_SMOKE" Enter`,
  `capture-pane -p`, and `kill-server` complete, and `capture-pane` sees the
  ConPTY shell banner, command echo, and command output.
- Additional runtime smokes now pass for multiple Windows panes and jobs:
  horizontal `split-window`, independent `send-keys`/`capture-pane` for both
  panes, `select-layout` plus `swap-pane` pane reordering,
  `link-window`/`unlink-window` linked windows across sessions,
  `break-pane`/`join-pane` pane movement between windows, `respawn-pane -k`,
  `respawn-window -k`, `resize-pane -x`, `kill-pane` cleanup of an active
  Windows child process tree,
  `pane_current_command` for both the shell and an active child process,
  with Windows console host helper processes such as `conhost.exe` and
  `OpenConsole.exe` ignored when choosing the pane's active command so an idle
  shell is not misreported after Ctrl+C,
  `send-keys C-c` interrupting both `timeout.exe` under `cmd.exe` and a
  PowerShell `Start-Sleep` active child process, startup
  `pane_current_path` metadata, dynamic `pane_current_path` refresh after a
  pane-local `cd`, valid `new-window -c` Windows pane cwd selection including
  a cwd containing spaces and shell metacharacters plus invalid cwd fallback,
  near-`MAX_PATH` cwd selection for panes and `run-shell -c`, junction cwd
  selection for panes and jobs, symlink cwd selection for panes and jobs when
  the host permits symlink creation, local `\\?\C:\...` cwd prefix
  normalization for panes and jobs, `cmd.exe` pane cwd selection through
  a local administrative-share UNC cwd when available, PowerShell
  `default-shell` command windows started in a local administrative-share UNC
  cwd when available,
  a PowerShell `default-shell` pane plus shell-command window exercising the
  non-`cmd.exe` `-c` command wrapper,
  synchronous `run-shell` writing a file including a quoted absolute target
  path containing spaces and shell metacharacters, `run-shell` command-client
  stdout, `run-shell -E` interleaved stdout/stderr output, asynchronous
  `run-shell -b` completion, `run-shell -c` valid Windows cwd selection
  including cwd paths containing spaces and shell metacharacters, local
  administrative-share UNC cwd when available, plus invalid cwd fallback, and
  `if-shell` running a follow-up command. The
  runtime smoke also covers
  the default Windows configuration search path, startup `-f` configuration
  loading, `source-file`, Windows `%VAR%` plus wildcard `source-file`
  expansion, `after-new-window` hook execution through `run-shell`,
  `load-buffer`/`save-buffer` file transfers including
  command-client stdout plus a 64KB binary SHA256 round-trip,
  `pipe-pane` output capture including a quoted absolute target path with
  spaces and shell metacharacters plus a 160-line bulk output case,
  `pipe-pane -I` injecting command output into a pane, and `pipe-pane -IO`
  using the duplex pipe bridge.
  Copy-mode search plus `copy-line-and-cancel`, repeat-count `select-line`
  multi-line selection with `copy-selection-and-cancel`, rectangular
  selection, `paste-buffer`, and `copy-pipe-line-and-cancel` are covered as
  well. With an attached client,
  the same smoke now verifies native Windows clipboard writes through
  `set-buffer -w`, clipboard imports through `refresh-client -l`, and pane
  OSC 52 clipboard sequences updating both the Windows clipboard and tmux
  paste buffer. A basic `tmux -C attach` smoke now verifies control-mode stdin
  command parsing, `%output` delivery,
  command output from `display-message` and `capture-pane`, and a session
  subscription created with `refresh-client -B` emitting
  `%subscription-changed` after `rename-session`. It now also covers
  `refresh-client -B` window and pane format subscriptions, `refresh-client
  -C` client and per-window resize updates, and `refresh-client -A` pane
  pause/continue output flow control. A basic ordinary `attach` smoke now
  verifies that a redirected Windows attached client can open, send input
  through the stdio bridge to a pane, run a `display-popup -E` command through
  the popup `JOB_PTY` path, validate `display-popup -d` cwd selection
  including a cwd containing spaces and shell metacharacters plus invalid cwd
  fallback, choose a `display-menu` item from the attached
  client's stdin path, and exit through `detach-client`.
  The attached-client path also covers `choose-buffer` entering buffer mode,
  accepting a selection from redirected client stdin, and pasting the selected
  buffer back into the pane, plus `choose-tree` entering tree mode and running
  the selected item's command template. `command-prompt -b` is covered too,
  including prefilled prompt input accepted through redirected client stdin and
  `%1` command-template substitution. `confirm-before -b` covers the single-key
  confirmation prompt
  callback path.
- Windows pane and job spawning now preserve tmux's resolved `default-shell`
  instead of unconditionally falling back to `ComSpec` for command strings or
  storing `/bin/sh` in pane metadata. Command strings use `cmd /d /c` for
  `cmd.exe` and `-c` for other shells.
- Windows pane spawning now validates the resolved pane cwd before starting the
  ConPTY child. If the directory is missing or invalid, it falls back through
  tmux's Windows default-cwd discovery instead of failing `CreateProcess`
  outright. Local `\\?\C:\...` cwd prefixes are normalized to drive-letter
  paths before spawning because Windows console shells do not treat the device
  prefix as a usable current directory. Local `\\?\UNC\...` cwd prefixes are
  normalized to regular UNC paths.
- Windows path validation now uses a UTF-8-to-wide path helper and
  `GetFileAttributesW`, so explicit extended paths and near-`MAX_PATH` cwd
  directories are checked without the ANSI `MAX_PATH` truncation behavior.
  Local drive cwd paths at or beyond the Windows process current-directory
  limit are treated as unsupported and fall back to the default cwd rather than
  failing pane or job spawn.
- Windows background jobs now use the same cwd fallback behavior when a caller
  supplies a start directory: invalid directories fall back through the Windows
  default-cwd discovery and update `PWD` before launching the redirected
  process or ConPTY job. They use the same local `\\?\C:\...` cwd
  and `\\?\UNC\...` cwd normalization as panes.
- Windows `pipe-pane` process launches also validate the client/session cwd and
  fall back to the default Windows cwd before starting the redirected process.
- The Windows `cmd.exe` detector and shell-command argv builder now live in
  `compat/win32-command.[ch]`, so pane spawning, jobs, `pipe-pane`, and client
  `-c` exec share one command wrapping rule. For `cmd.exe /d /c` and
  `cmd.exe /d /k`, the command string after `/c` or `/k` is preserved rather
  than CRT-escaped so quoted redirection targets with spaces and shell
  metacharacters keep working. When panes or jobs start `cmd.exe` from a UNC
  cwd, tmux launches cmd from a local fallback cwd and prefixes the command
  with `pushd "\\server\share\path"` so cmd's temporary drive mapping gives
  relative commands the requested UNC directory.
- Windows pane/job/pipe fallback shell selection now validates `ComSpec` and
  `SHELL` with `checkshell()` before using them, and otherwise falls back to
  the shared default-shell resolver.
- `pipe-pane` uses the same Windows shell selection rules, so formatted pipe
  commands run through the target session's `default-shell` rather than the
  process environment's `ComSpec` unless a fallback is needed.
- Environment variable lookup is case-insensitive on Windows, matching the
  platform and preventing duplicate child entries such as both `Path` and
  `PATH` when tmux updates the process environment for panes and jobs.
- Popup/menu and client exec shell fallbacks now use the shared startup shell
  resolver on Windows, avoiding `/bin/sh` in popup pane metadata, display-menu
  popups, and `MSG_SHELL`/`MSG_EXEC` client messages when `default-shell` is
  invalid.
- Startup path-list expansion now has Windows path handling for configuration
  and socket path lists: it supports `~\` and `$VAR\...` expansion, accepts
  semicolon-separated lists, and does not split drive-letter paths such as
  `C:\Users\name\tmux.conf` at the colon. Windows `%VAR%\...` environment
  references are also expanded when they appear at the start of a path.
- The native Windows default configuration search path now checks
  `%PROGRAMDATA%\tmux\tmux.conf`, `%APPDATA%\tmux\tmux.conf`, and
  `~\.tmux.conf` instead of Unix `/etc/tmux.conf`.
- Windows `default-shell` validation now asks `SearchPath` to resolve the
  executable, so nonexistent shells, literal `%ComSpec%` strings, and command
  strings with arguments are rejected before they reach pane or job spawning.
- `cmd-parse.y` no longer requires Unix `pwd.h`/`unistd.h` in native Windows
  builds. Parser `~` expansion uses `HOME` or the Windows home resolver for the
  current user, treats backslash as a path separator, and leaves `~user`
  expansion to Unix builds.
- On Windows, the command parser also recognizes `%VAR%` environment references
  outside single quotes. This allows configuration paths such as
  `%APPDATA%\tmux\tmux.conf` without breaking tmux condition directives like
  `%if` or pane tokens such as `%1`.
- Shared file read/write path resolution now recognizes Windows absolute paths
  and `~\...` paths before adding the client's current directory. This keeps
  commands such as `source-file C:\Users\name\tmux.conf` from being treated as
  relative paths after glob expansion.
- `path_is_absolute()` centralizes native absolute-path detection (`C:`,
  `\...`, `/...`) and is used by source-file, shared file I/O, status history,
  and pane working-directory resolution.
- Windows file transfers avoid libevent bufferevents on CRT file descriptors.
  Server-side `file_read`/`file_write` opens regular files directly, and
  client-side file protocol requests synchronously read or write both regular
  files and stdout/stderr stream targets such as `-`.
- Status prompt history-file resolution accepts Windows absolute paths and
  `~\...`, and automatic window naming now strips Windows path prefixes from
  pane commands such as `C:\Windows\System32\cmd.exe`.
- Popup editor temporary files now use the native Windows temp directory via
  `GetTempPath`/`GetTempFileName` instead of `_PATH_TMP`, pass that directory
  as the popup job cwd, and read/write the edit buffer in binary mode.
- Windows builds now override fallback path constants that otherwise point at
  Unix locations: `_PATH_BSHELL` uses `cmd.exe`, `_PATH_DEVNULL` uses `NUL`,
  `_PATH_TTY` uses `CON`, `_PATH_DEFPATH` uses the Windows system directories,
  and `_PATH_VI` uses `notepad.exe`. `VISUAL`/`EDITOR` detection also treats
  backslashes as path separators when selecting vi/emacs key defaults.
- Invalid or missing client working directories now fall back through Windows
  home and temporary-directory discovery instead of storing `/` as the client
  or session cwd.
- Clipboard integration now has a native Windows path through
  `compat/win32-clipboard.[ch]`: `set-clipboard` first tries to set
  `CF_UNICODETEXT` directly, `refresh-client -l` can read the Windows
  clipboard into a paste buffer, and OSC 52 clipboard queries can be answered
  from the Windows clipboard before falling back to terminal OSC 52. Clipboard
  open calls retry briefly so transient Windows clipboard ownership does not
  immediately break set/get operations.
- `proc_set_signals()` now registers a Windows console control handler so tmux
  process control events map Ctrl+C/Ctrl+Break to the existing SIGINT path and
  console close/logoff/shutdown events to the SIGTERM path.
- Attached Windows console clients proxy Ctrl+C console control events through
  a client-local socketpair back into the libevent thread, then send
  `MSG_STDIN` with byte `0x03` to the server. This keeps imsg writes out of
  the console control handler thread while giving raw-input panes the same ETX
  byte when Ctrl+C is generated by a real attached console.
- ConPTY panes now create their root process with `CREATE_NEW_PROCESS_GROUP`,
  and the Windows PTY input bridge maps incoming `C-c`/`0x03` to a targeted
  `CTRL_BREAK_EVENT` for that process group. This gives `send-keys C-c` an
  interrupt path for active Windows console children; the smoke now verifies
  both a `cmd.exe` child and a PowerShell child. It still forwards the ETX byte
  into the ConPTY stream for applications that process raw input themselves.
  `send-keys C-Break` is now parsed as a `Break` key with the Ctrl
  modifier and directly triggers the same process-group `CTRL_BREAK_EVENT`
  path for native Windows panes. The real-console attach smoke also sends
  Ctrl+C and Ctrl+Break control events through an `AllocConsole` attached
  client and verifies that both interrupt cmd-hosted and PowerShell children
  in the pane.
- `osdep-windows.c` now resolves `conpty:<pid>` pane tty markers to the active
  descendant process image path with `QueryFullProcessImageName`, improving
  `pane_current_command`, choose-tree searches, and automatic rename fallbacks
  for native Windows panes.
- `pane_current_path` now has a best-effort native Windows query for ConPTY
  panes: `osdep-windows.c` parses `conpty:<pid>`, follows the ConPTY shell
  wrapper to the active child process, reads that process PEB with
  `NtQueryInformationProcess`/`ReadProcessMemory`, prefers the remote
  `CurrentDirectory.DosPath`, falls back to a duplicated current-directory
  handle, normalizes the result, and then falls back to the pane's recorded
  start directory if the unsupported OS-level query fails.
- The default `lock-command` now uses the Windows workstation lock command
  (`rundll32.exe user32.dll,LockWorkStation`) instead of the Unix `lock -np`
  default while still allowing user overrides.

## Parity audit snapshot

This is not a completion claim; it is the current evidence map for the Windows
port.

- Covered by `windows/smoke-runtime.ps1`: detached command clients, startup
  configuration, `source-file`, environment expansion, hooks, buffers and file
  transfer, pane send/capture, split/swap/break/join/link window operations,
  respawn and kill cleanup, popup/menu/choose/prompt/confirm attached-client
  UI, copy-mode search/navigation plus line/multi-line/rectangle/copy-pipe
  flows, paste-buffer,
  pipe-pane output including a bulk-output case plus `-I`/`-O` paths,
  control-mode attach/subscriptions/resize/flow control, Windows clipboard
  integration, pane cwd selection including near-`MAX_PATH` cwd and dynamic
  `pane_current_path`, active-child `pane_current_command`, and an initial
  pane `C-c`/`C-Break` process-group interrupt path. The smoke also has a
  console-style attach hook probe that verifies `client-attached` and
  `client-detached` fire for an ordinary `tmux attach` process, plus an
  `AllocConsole` real-console attach probe that sends keyboard input through a
  real console stdin path into a pane, resizes the attached console, verifies
  the client size update, sends input again after the resize, and then runs a
  short repeated-resize churn sequence with input after each resize. It also
  sends real-console Ctrl+C and Ctrl+Break control events through the attached
  client and verifies that cmd-hosted and PowerShell children are interrupted
  before the pane accepts follow-up input, and verifies real-console raw
  Ctrl+C delivery as an ETX byte to a raw-input PowerShell probe.
- Covered by `windows/stress-runtime.ps1`: repeated packaged smoke loops.
  Covered by `windows/soak-runtime.ps1`: a mixed pane/job/resize/pipe workload
  soak. The local release gate has passed packaged smoke, stress, and soak
  iterations with artifact hash verification.
- Covered by `windows/console-attach-soak.ps1`: a longer real-console attach
  run using `AllocConsole`, Ctrl+C/Ctrl+Break interruption, repeated resize
  churn, repeated attach/detach cycles, client size verification, pane input
  after the resize sequence, and raw Ctrl+C ETX delivery in the same session.
- Partially covered: pane/job lifecycle behavior, attached-client stdio bridge,
  popup/job PTY behavior, command prompt/menu timing, and Windows process-tree
  cleanup. These have positive smoke/client-stress/stress/job-stress/soak
  coverage but need longer-running mixed-workload regression coverage.
- Not yet Linux parity: real-console attach coverage beyond the current
  focused smoke and console soak, full raw-mode semantics beyond the current
  pane and real-console ETX probes, signal/process-group behavior beyond the
  current `C-c`/ETX/`C-Break` path, Autotools/release integration, signed
  MSI/MSIX-style installer packaging, and broad regression coverage beyond the
  current packaged smoke/stress/soak loop.

## Remaining native Windows work

`windows/PARITY-AUDIT.md` is the current source of truth for completion status
and non-completion items. The notes below are retained as implementation
history and should be read against that audit rather than as a fresh release
checklist.

- Complete and test the Windows process creation path in `spawn.c` for all pane
  creation and respawn combinations. The direct ConPTY path and Windows
  respawn cleanup exist, the runtime smoke now validates `respawn-pane -k` and
  `respawn-window -k`, and the POSIX child path is isolated, but full native
  builds and regressions still need to validate every command mode.
- Add the same Windows process creation path in `job.c` for `run-shell`,
  `if-shell`, popups, and commands that request `JOB_PTY`. The first `JOB_PTY`
  path, non-PTY process path, and popup transfer exist, the runtime smoke now
  validates `display-popup -E` on an attached client, but full native builds
  and regressions still need to validate every job mode.
- Replace Unix-domain server sockets and fd passing with a Windows IPC design
  that still supports tmux client/server commands and control mode. The first
  loopback endpoint helper, no-fd-passing imsg path, and client/server/proc
  integration hooks exist, and stdin/stdout identify messages can duplicate
  Windows handles across processes. Control-mode and ordinary terminal
  stdin/stdout now have a socket bridge; the remaining IPC work is to validate
  control-mode and attached-client paths in a full native build, and harden
  resize/event coverage for Windows console clients. Foreground
  `CLIENT_NOFORK` startup and the first mutexed detached
  `tmux -D -S <endpoint>` background launch path are wired to the native
  listener; detached command clients now have an initial runtime smoke, but
  broader lifecycle validation is still required.
- Finish packaging the Windows build dependencies. The native PowerShell build
  path now emits `tmux.exe` with a bison-generated parser and real libevent,
  including MSYS2 libevent auto-discovery for the common local toolchain
  layout, and `windows/package-mingw.ps1` can produce a smoke-tested portable
  directory or `.zip` with the required MinGW/libevent DLLs and SHA256
  metadata. `windows/install-portable.ps1` provides a conservative user-level
  install/uninstall path for that portable zip, verifies package and installed
  file hashes, and only updates the user PATH when explicitly requested.
  `windows/release-check.ps1` now ties those steps into a local gate with
  artifact hash verification, command-surface audit, optional unsigned MSIX
  packaging, artifact verification, zip install/uninstall verification, and
  optional respawn stress, job stress, client lifecycle stress, config parser
  stress, packaged stress iterations, and soak runs, then emits JSON summaries
  for CI artifact retention.
  `.github/workflows/windows-mingw.yml` is an initial hosted CI gate for that
  path. `windows/RELEASE.md` records the current minimum release policy, and
  `.github/workflows/windows-release.yml` provides a manual draft-release path.
  `Makefile.am` includes the Windows scripts and release docs in `EXTRA_DIST`
  so source tarballs carry the Windows release tooling. A production
  trusted-code-signing certificate, signed release artifacts, hosted CI run
  history, and branch/release policy still need final upstream integration.
- Replace terminal-mode operations based on `termios`, `tcgetattr`,
  `tcsetattr`, and Unix signals with Windows console and process-group
  equivalents. Pane-delivered `C-c` and `C-Break` now have an initial
  process-group control event path, with ETX forwarding for `C-c`, and the
  real attached console smoke now covers Ctrl+C/Ctrl+Break child interruption
  plus raw Ctrl+C ETX delivery through the client signal proxy. Broader signal
  parity still needs coverage for shell-specific handlers, additional raw-mode
  applications, and longer real attached-console runs.
- Audit all remaining POSIX-only calls (`fork`, `waitpid`, `kill`, `socketpair`,
  `sigaction`, `ioctl`, `chdir`/path handling, uid/gid permission checks) and
  introduce scoped Windows shims only where the existing architecture needs
  them.
- Broaden Windows regression coverage beyond the current runtime smoke so it
  proves longer-running real-console attached clients, richer control-mode
  pane/window subscriptions, richer copy-mode
  navigation, configuration parse edge cases beyond the current config parser
  stress, and longer-lived mixed pane/job/client lifecycles beyond the current
  packaged client-stress/job-stress/stress/soak loop.
