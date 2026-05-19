---
name: Use this issue template
about: Report a tmux-win Windows-native issue
title: ''
labels: ''
assignees: ''

---

### Issue description

Please read `.github/CONTRIBUTING.md` before opening an issue.

If you have upgraded, make sure your issue is not covered in the local CHANGES
file or the Windows verification notes.

Describe the problem and the steps to reproduce. Add a minimal tmux config if
necessary. Screenshots can be helpful, but no more than one or two.

Do not report bugs without reproducing on a tmux-win build from this repository.

### Required information

Please provide the following information. These are **required**. Note that bug reports without logs may be ignored or closed without comment.

* tmux version (`tmux -V`).
* Windows version.
* Terminal host in use (Windows Terminal, conhost, PowerShell, cmd, etc).
* Exact `tmux.exe` path under test.
* Whether the binary came from `dist\tmux-win32-portable`.
* Logs from tmux (`.\tmux.exe kill-server; .\tmux.exe -vv new`).
