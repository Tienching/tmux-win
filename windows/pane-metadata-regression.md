# Windows pane metadata truncation

Native `list-panes` could stop formatting after `pane_current_path` when the
two process-tree snapshots used for command and directory lookup exhausted
the 100 ms format limit. The command still exited successfully with an
unfinished record. This was observed directly on office-pc3, independently
of HTTP or UI rendering, including a verbose `reached time limit (109)`.

The fix acquires a process snapshot before the outer format timer starts,
reuses it for 500 ms, and pins it across nested expansions. The format time
limit is unchanged. At most one cached snapshot handle is retained; replacement
closes the old handle and failed acquisition is also rate-limited. All helpers
run on the existing server thread. Current command/path reporting may lag a
process-tree change by up to 500 ms; this data is not used for authorization.

## Verification

- `windows/process-snapshot-test.ps1 -CC <mingw-gcc>` exercises real source
  with deterministic slow snapshot acquisition, reuse, expiry, nesting,
  failure backoff, handle replacement and recovery.
- `node windows/pane-metadata-regression.cjs <tmux.exe> 60` creates nine
  isolated test sessions on a random named server and checks every 19-field
  inventory and pane PID. It never attaches to or detaches a user session.
  Cleanup only addresses the generated server label.
- Windows compilation and Linux POSIX compilation must both succeed.

Local comparison on 2026-09-21: the pre-fix executable had one incomplete
inventory in 15 queries (mean 762 ms). The fixed executable had zero in 60
queries (mean 352 ms). These timings are machine-specific, not universal
latency guarantees. Existing peer watcher tests passed; the separate pipe
ownership fixture failed with error 2 on both the unchanged baseline and
this branch. Full release validation is therefore not claimed.

No live runtime replacement was performed. Deploying a native server change
requires planning around existing terminal processes; this test is not
authorization to restart them.
