# Windows release checklist

This checklist is the current release policy for the native Windows port. It is
not a claim of full upstream parity by itself; it describes the minimum evidence
needed before publishing Windows artifacts.

## Required local gate

Run the release verifier against a fresh build or an explicitly selected
`tmux.exe`:

```powershell
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"
.\windows\release-check.ps1 `
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
  -BuildMsix
```

For a previously built binary, add `-SkipBuild` only when the exact binary under
test is already known and recorded.

For manual user validation and troubleshooting commands, see `windows/VERIFY.md`.

On a desktop machine with Windows Terminal available, add
`-RunVisualTerminalVerify` to the local gate to open a real visible terminal
and confirm attached-client rendering through UI Automation. Do not enable this
step in headless CI.

The gate must pass all of these steps:

- Native MinGW/libevent build, unless `-SkipBuild` is deliberately used.
- Portable zip packaging and packaged runtime smoke.
- Zip `.sha256` sidecar verification.
- Portable manifest file hash verification.
- Command-surface and Windows default-option audit.
- Unsigned MSIX package generation with `makeappx.exe`.
- Targeted `respawn-pane -k` stress.
- IPC endpoint ACL and token rejection stress.
- Job stdout/stderr and background-process stress.
- Multi-client command/control/attach lifecycle stress.
- Signal matrix stress for `C-c`, `C-Break`, cmd/PowerShell children, and raw
  ETX delivery.
- Config parser/source-file stress.
- Portable zip install/uninstall verification.
- Packaged stress iterations.
- Mixed runtime soak.
- Real-console attach soak.
- Clipboard contention stress.
- Optional visible Windows Terminal attach check when
  `-RunVisualTerminalVerify` is explicitly requested.

Then verify the produced artifact set:

```powershell
.\windows\verify-release-artifacts.ps1 -RequireMsix
```

Run the remaining evidence audits and feed them into the completion audit:

```powershell
.\windows\ipc-boundary-audit.ps1 `
  -Tmux .\dist\tmux-win32-portable\tmux.exe `
  -Output .\dist\ipc-boundary-audit.json
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
.\windows\completion-audit.ps1 `
  -SigningSummary .\dist\signing-audit.json `
  -IpcBoundarySummary .\dist\ipc-boundary-audit.json `
  -LinuxParitySummary .\dist\linux-parity-matrix.json `
  -LinuxBehaviorSummary .\dist\linux-behavior-parity.json `
  -HostedCiSummary .\dist\hosted-ci-audit.json `
  -SourceStateSummary .\dist\source-state-audit.json
.\windows\verify-release-artifacts.ps1 `
  -RequireMsix `
  -RequireSigningAudit `
  -RequireCompletionAudit `
  -RequireIpcBoundaryAudit `
  -RequireLinuxParity `
  -RequireLinuxBehaviorParity `
  -RequireHostedCiAudit
```

Generate release notes after the evidence audits so they include signing, IPC,
Linux parity, source-state, hosted CI, and completion-audit status:

```powershell
.\windows\write-release-notes.ps1
```

On an elevated release-validation host, add `-RunSystemTaskProbe` to verify a
temporary SYSTEM scheduled task cannot read the endpoint token. For full
multi-user coverage, run the same audit with
`-OtherUserCredential (Get-Credential)` using a real second local or domain
account, or with `-CreateTemporaryLocalUser` on an elevated local validation
host.

## Required artifacts

Keep these files from `dist`:

- `tmux-win32-portable.zip`
- `tmux-win32-portable.zip.sha256`
- `tmux-win32-portable\manifest.json`
- `release-check.json`
- `command-surface.json`
- `tmux-win32.msix`
- `tmux-win32.msix.json`
- `signing-audit.json`
- `windows-release-notes.md`
- `ipc-boundary-audit.json`
- `linux-parity-matrix.json`
- `linux-behavior-parity.json`
- `hosted-ci-audit.json`
- `source-state-audit.json`
- `completion-audit.json`

The release notes or release record must include:

- tmux version from `release-check.json`.
- Portable zip SHA256 from `release-check.json`.
- MSIX SHA256 from `tmux-win32.msix.json`.
- Whether the MSIX is signed.
- Exact release-check command line.
- Host OS and toolchain version, or hosted CI run link.
- GitHub Actions summary values, when artifacts come from hosted CI.
- Completion audit status and missing-item count.

## Signing

Unsigned MSIX artifacts are packaging-validation artifacts, not production
installers. A production installer requires a trusted code-signing certificate.

For a signed MSIX, pass a certificate whose subject exactly matches the MSIX
manifest publisher:

```powershell
.\windows\package-msix.ps1 `
  -Package .\dist\tmux-win32-portable `
  -Output .\dist\tmux-win32.msix `
  -SummaryPath .\dist\tmux-win32.msix.json `
  -Publisher "CN=Example Publisher" `
  -Sign `
  -CertificatePath .\codesign.pfx `
  -CertificatePassword "<password>"
```

The script checks `-Publisher` against the signing certificate subject before
calling `signtool.exe`.

## Hosted CI

The GitHub Actions workflow `.github/workflows/windows-mingw.yml` must complete
successfully before publishing artifacts. A local release-check pass is not a
substitute for hosted CI because hosted runners validate a clean checkout,
toolchain discovery, artifact upload paths, and the generated Actions step
summary with release hashes and command-surface counts.

The hosted CI audit must target the release commit SHA. The GitHub Actions
workflows derive that SHA from the checked-out tree with `git rev-parse HEAD`
and store it in `hosted-ci-audit.json`.

For release-candidate artifacts, run `.github/workflows/windows-release.yml`
manually with a tag or ref. By default it builds, verifies, writes release
notes, writes the Actions summary, audits hosted CI history, tries the Linux
surface and focused behavior parity checks when WSL and Linux `tmux` are
available, and uploads artifacts. It can create a GitHub release only as a
draft until production signing is configured. For a non-draft release, the
workflow preflights completion-audit `complete`, hosted CI `passed`, clean
source-state, and trusted signing before invoking `gh release create`. Those
non-draft publication checks are only applied when `create_release=true` and
`draft=false`; build-only release-candidate runs still write the audit JSON and
artifact bundle without claiming publication readiness.

## Do not publish when

- `release-check.ps1` fails or any summary JSON is missing.
- `verify-release-artifacts.ps1` fails.
- `verify-release-artifacts.ps1 -RequireCompletionComplete` fails for a
  production or non-draft release.
- `git diff --check` reports whitespace errors.
- A `tmux.exe` process from the release gate remains after cleanup.
- `ipc-boundary-audit.json` is missing or has failed checks.
- `linux-parity-matrix.json` is missing when WSL/Linux tmux parity evidence is
  required for the release record.
- The MSIX is unsigned but the release is advertised as an installer.
- The signing certificate subject does not match the MSIX publisher.
- Hosted CI has not produced a green run for the release commit.
- `verify-release-artifacts.ps1 -RequireHostedCiGreen` fails for a production
  release.
- `source-state-audit.json` reports a dirty working tree for production
  release artifacts.
- The release workflow is asked to create a non-draft release before production
  signing and completion-audit closure are configured.

## Current open release work

- Obtain and configure a production trusted code-signing certificate.
- Decide whether the official installer format is signed MSIX, MSI, or both.
- Wire signed artifact publication into the upstream release flow.
- Produce and record a green hosted Windows CI run for the release commit.
- Produce final release artifacts from a clean committed source tree.
- Attach Linux surface and focused behavior parity JSON to release records when
  WSL/Linux `tmux` is available on the validation host.
- Broaden IPC boundary evidence with domain-account policy variants on a lab or
  hosted validation machine.
