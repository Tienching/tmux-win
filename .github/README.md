# tmux-win GitHub Metadata

This directory contains GitHub issue templates and Windows CI workflows for:

	https://github.com/Tienching/tmux-win

Use this repository for Windows-native tmux issues, Windows build problems,
Windows runtime behavior, release packaging, and CI failures.

Before opening an issue, check:

- `README`
- `windows/VERIFY.md`
- `windows/PORTING.md`
- `windows/RELEASE.md`

Useful local checks:

```powershell
.\tmux.exe -V
.\windows\verify-portable.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe
.\windows\smoke-runtime.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -TimeoutSeconds 180
```

For debugging, run tmux with `-vv` and attach the generated
`tmux-server*.log`, `tmux-client*.log`, and `tmux-out*.log` files to the issue.
