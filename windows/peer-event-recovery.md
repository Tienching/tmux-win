# Windows IPC peer recovery regression

The Windows timer callback must arm its next wakeup before invoking a callback
that can destroy the peer. Destruction cancels the timer. No access to the peer
is allowed after dispatch returns.

A stale IPC socket left in libevent's select set can cause every event-loop call
to fail with WSAENOTSOCK. Recovery removes the invalid peer's two event watchers,
invalidates its descriptor and blocks further sends before teardown (avoiding a
close of a reused handle or re-registration of the invalid watcher),
and dispatches the existing disconnect path. Inconclusive polling errors retain
the existing retry behavior rather than disconnecting a healthy peer. Healthy
peers and session PTYs are not removed. Unclassified event-loop failures back
off rather than spin.

## Targeted tests

With a MinGW UCRT compiler and matching libevent DLLs on PATH:

```powershell
./windows/peer-event-recovery-test.ps1 -CC gcc
```

This compiles the actual proc.c into deterministic lifecycle tests and a real
Winsock/libevent integration test. The latter closes its own watched socket,
asserts the original WSAENOTSOCK failure, then verifies recovery and delivery on
an unrelated socket. It does not attach to an existing tmux server.

## Isolated native terminal test

```powershell
node ./windows/peer-event-e2e.cjs ./tmux.exe <path-to-node-pty-module>
```

Use a supported Node version and a clean test-process PATH containing the
matching runtime DLLs, System32 and Windows PowerShell. The test creates its own
random socket label and two PowerShell sessions. Thirty alternating attaches
verify input/output and graceful/abrupt client disconnects; twenty query
cancellations are attempted, with actual cancellations reported separately from
queries completed before cancellation. It checks that both pane PIDs survive
and no attached clients remain, then kills only its own server. Timings are
local native measurements, not browser-to-Hub latency measurements.

These tests establish the code defect and recovery mechanism. They do not prove
which historical operation first invalidated a particular production socket.
Installing a new executable does not repair an already running old server.
