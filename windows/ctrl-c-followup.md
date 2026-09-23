# Ctrl-C release blocker investigation

The existing signal matrix reproduces a PowerShell Start-Sleep interruption
failure on baseline and peer-recovery builds. This is independent of the Hub
liveness fix. Tests use isolated named servers, never existing user panes.

Compare unchanged ETX forwarding with the existing additional Ctrl-Break.
Require processed Ctrl-C, raw ETX, explicit Ctrl-Break, post-interrupt input and
cross-pane isolation before accepting a fix. Never replace interruption with
process termination or relax assertions to obtain a pass.

Agent remains 0.5.18. Packaging, independent review and release validation are
required before including the candidate executable in Tencent Hub.

## User acceptance clarification

The end-to-end acceptance target is an already-connected ioa-ssh-cli session:
Ctrl-C must interrupt only the remote foreground task, not exit ioa-ssh-cli,
the SSH connection, the local shell or tmux. Verify at least 20 interruptions,
exactly-once delivery, continued input, unchanged connection identity and a
second pane unaffected. Local PowerShell checks cannot substitute for this.

## Current evidence (2026-09-22)

- Unmodified dc88a5f2: timeout.exe interrupt succeeds; PowerShell Start-Sleep
  fails to return within the existing 12-second assertion.
- ETX-only candidate: timeout.exe no longer interrupts.
- ETX-only plus removing the pane CREATE_NEW_PROCESS_GROUP flag: timeout.exe
  still does not interrupt. The server also starts in a new process group;
  inherited Ctrl-C-disabled state is a hypothesis requiring a targeted probe.
- Removing the pane group also invalidates the old explicit Ctrl-Break group
  target. These experimental changes are NOT merge/release ready.
- Independent review requires preserving raw/processed semantics, restoring
  inherited state safely and moving explicit console operations out of the
  shared server process. Do not reintroduce duplicate Ctrl-Break as a workaround.
- Official IOA CLI updated 0.7.93 to 0.7.94; host authorization succeeded.
  The independent jumphost probe timed out before remote entry; no interactive
  IOA acceptance result exists yet, and no live user connection was touched.

## Proposed implementation: child-only console bootstrap

The ETX-only candidate plus a child wrapper that resets the inherited
Ctrl-C-ignore flag successfully interrupts both timeout.exe and PowerShell
Start-Sleep. Implement an internal tmux executable mode inside each pane's
ConPTY. It enables Ctrl-C before creating the real command, without changing
the shared server's console state. Its own handler keeps only the wrapper
alive; the child inherits enabled Ctrl-C, not the wrapper's handler function.
Preserve the pane process group for explicit Ctrl-Break, job containment,
Unicode command line, working directory, environment, and child exit status.
Forward ETX exactly once with no additional signal and no input batching.
Acceptance: processed and raw input, explicit Break, cross-pane isolation,
and real IOA connection survival for repeated remote-task interruption.

## Reviewed candidate validation

- ETX remains exactly one byte-path write, without additional Ctrl-Break.
- Explicit Break executes in a disposable helper, never attaches the server.
- Real-command startup has a five-second ACK and explicit inherited-handle
  allowlists. Failed startup clears the child tree/job before bounded PTY close.
- Candidate v4 full build and three signal-matrix rounds passed: each round
  checks 20 raw ETX, zero console control events, continued input, processed
  interruption, explicit Break, and an unaffected second-pane heartbeat.
- Lost-ACK fault injection returned ERROR_TIMEOUT in 5047ms and verified the
  real child exited; missing executable retained error2. Native Unicode cwd,
  environment, quoted arguments and exit37 passed. This fixture is now wired
  into release-check whenever signal-matrix validation is enabled.
- CLI spawn failure, respawn exit status and child cleanup passed. CLI -c
  resetting to home also reproduces with the installed baseline: it remains
  a separate known issue, not a successful cwd test or a new Ctrl-C regression.
- Independent review found no remaining code-level blocker after two rounds.
- RELEASE HOLD: real IOA remote-task interruption/connection-survival acceptance
  is not complete. The read-only jumphost probe timed out after 35 seconds;
  no further remote IOA operations, live session input or installed-file change.
- Agent remains 0.5.18. Full release suite and IOA acceptance are still required.
