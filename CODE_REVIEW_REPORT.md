# Code Review Report — `windows-port-release-candidate`

## 评审范围

- **基线**: `origin/master..HEAD`
- **变更规模**: 35 个未推送提交，126 个文件，**+28 270 / -286** 行
- **作者署名**: 35 个提交均为 `Codex <codex@localhost>`，提交时间集中在 2026-05-18 至 2026-05-19
- **评审方式**: 由 4 名专家 agent 并行覆盖以下维度（本报告由 lead 综合）：
  1. `reviewer-compat` — Win32 兼容层（`compat/win32-*`、`compat/imsg*`、`osdep-windows.c`）
  2. `reviewer-core` — tmux 核心 C 源码（`client.c`、`server-client.c`、`job.c`、`spawn.c`、`tty-term.c`、`utf8.c`、`tmux.h` 等）
  3. `reviewer-scripts` — `windows/*.ps1`（33 个，~14k 行）+ GitHub Actions + `Makefile.am` / `configure.ac`
  4. `reviewer-quality` — 提交历史、文档、版权与合入策略

---

## 执行摘要

变更整体目标清晰、工程意识较强（错误处理一致、JSON 摘要 + 哈希链、双重发布门禁、ACL+token 双重校验），但**存在多条红线问题，当前形态既无法以 PR 形式合入 tmux 上游 mainline，也不建议直接 push 到任何受信任远端发布**。最高优先级问题集中在三类：

1. **版权署名伪造**：27 个新增 `compat/win32-*.{c,cc,h}` 与 `osdep-windows.c` 的版权头全部写为 `Copyright (c) 2026 Nicholas Marriott <nicholas.marriott@gmail.com>`（tmux 上游主作者），但 git author 是 `Codex <codex@localhost>`。这是**合规级别的红线**，必须立即修复。
2. **运行期稳定性 Blocker**：Win32 兼容层至少 5 处会在长跑或 CI 场景下偶发崩溃/挂起（IO 线程与主线程的 use-after-free、ConPTY 关闭路径、`accept` 阻塞 5 秒、`WIFEXITED` 宏对 NTSTATUS 的语义错位等）。
3. **提交历史不可审计**：35 个提交全部 commit body 为空，含 4 个 `Retry …` 修补、多个仅记录运行证据的提交、一次塞入 ~28k 行的巨型起步提交。

下表汇总了各维度的整体评分：

| 维度 | 评分 | Blocker | Major | Minor |
|---|---|---|---|---|
| Win32 兼容层 (compat/) | 6.0/10 | 5 | 10 | 19 |
| tmux 核心 C 改动 | 6.5/10 | 2 | 约 25 | 约 30 |
| PowerShell 脚本 / CI | 6.0/10 | 6 | 10 | 10 |
| 文档 / 提交 / 版权 | 提交 2/10 · 版权 1/10 · 文档 5.5/10 | 3 类红线 | — | — |
| **整体合入度（mainline）** | **1/10** | — | — | — |
| **整体发布度（独立 fork）** | **6/10**（修复红线后可发布） | — | — | — |

---

## 一、Blocker 清单（合入/发布前必修）

### 1.1 合规与法务（最高优先级）

| 编号 | 位置 | 问题 | 修复 |
|---|---|---|---|
| Q-B1 | 27 个新增 `compat/win32-*.{c,cc,h}` + `osdep-windows.c` 文件头 | 把全新 Windows 适配代码归功于 tmux 上游主作者 Nicholas Marriott，构成著作权归属伪造 | 替换为真实贡献者署名，保留 ISC 许可文本不变 |
| Q-B2 | 全部 35 个提交 | author/committer 为 `Codex <codex@localhost>`，邮箱不可达，违反 DCO/可审计性 | `git rebase -i origin/master`，逐个 `commit --amend --author="Real Name <email>"` |
| Q-B3 | 全部 35 个提交 | commit message 仅有 subject，body 完全为空 | 重写每个提交的 body：why / 设计选择 / 影响范围 / 测试方法 |

### 1.2 Win32 兼容层运行期 Blocker

| 编号 | 位置 | 问题 | 修复方向 |
|---|---|---|---|
| C-B1 | `compat/win32-stdio.c:375-390`（`win32_stdio_bridge_close`） | 顺序为 `CancelSynchronousIo → _close(fd) → WaitForSingleObject(thread)`；线程仍可能在 `_close` 之后对已关闭甚至被复用的 HANDLE 发起 `ReadFile/WriteFile`，触发句柄复用 UAF | 先 `WaitForSingleObject` 等线程退出再 `_close`；或仅 shutdown bridge socket 让 `recv` 返回 0 让线程自然退出 |
| C-B2 | `compat/win32-pty.c:93-121`（`win32_pty_send_ctrl_break`） | IO 线程内调 `FreeConsole/AttachConsole`，影响**整个进程**的控制台关联，与其它线程的 `GetStdHandle/Console API`、`SetConsoleCtrlHandler` 形成进程级竞态，可能导致 tmux server 自身失去控制台 | 改用辅助进程 / 命名事件，或全局互斥保证同时只有一处在做 detach/attach console |
| C-B3 | `compat/win32-conpty.c:175-178`（`win32_close_pseudoconsole` 超时） | 1 s 超时后空跳过 + `CloseHandle(thread)`，工作线程仍在 `ClosePseudoConsole` 内部访问内核对象，紧接 `memset(pty,0,…)` | 超时则 detach 线程句柄到全局清理列表，不再 `CloseHandle`；进程退出统一回收 |
| C-B4 | `compat/win32-process.c:279-286`（`win32_process_spawn` 失败回退） | IO 线程已开始 `recv((SOCKET)process->bridge_socket,…) / WriteFile((HANDLE)process->input,…)`，主线程同时把这些字段 set NULL，存在 data race + UAF | IO 线程参数改为值拷贝（专用 thread-arg struct）；`close` 前 `WaitForSingleObject` 两个线程 |
| C-B5 | `compat.h` `WIFEXITED/WEXITSTATUS/WIFSIGNALED` 宏 | 直接套用 POSIX 位图，但 Win32 `GetExitCodeProcess` 返回 32-bit DWORD，常见 `0xC0000005`（AV）、`0x40010005`（DBG_CONTROL_C）等 NTSTATUS 在宏下完全错位，server-client 信号路径会连锁错误 | Windows 不复用 POSIX 宏；提供 `win32_exit_code(unsigned long, int *signaled)` 显式 API；或把 ExitCode 编码为 `(code << 8)` 后再用宏 |

### 1.3 tmux 核心层 Blocker

| 编号 | 位置 | 问题 | 修复方向 |
|---|---|---|---|
| K-B1 | `popup.c` `enum dragging` `OFF/MOVE/SIZE` 改名为 `DRAG_SIZE` | 为绕开 Windows 头宏，**对 POSIX 也生效**，纯命名污染上游 | 用 `#ifdef _WIN32 #undef SIZE #endif` 局部解决 |
| K-B2 | `server-client.c:2255-2261` `MSG_RESIZE` 处理 | `server_client_update_latest(c)` 与 `tty_resize(&c->tty)` 顺序在 POSIX 路径下被调换，影响 latest-client 选择与 resize 的 race | 保留原顺序，新增 Windows 行为放 `#ifdef` 内 |
| K-B3 | `utf8.c` / `tmux.h:3719-3720` | 公共 API `utf8_towc/utf8_fromwc` 形参类型从 `wchar_t` 改为 `utf8_wchar`，污染 mainline ABI/源码兼容性 | 内部用 `utf8_wchar`，对外签名保持 `wchar_t`，加 `_Static_assert(sizeof(utf8_wchar)==sizeof(wchar_t))` |

### 1.4 PowerShell / CI Blocker

| 编号 | 位置 | 问题 | 修复方向 |
|---|---|---|---|
| S-B1 | `.github/workflows/windows-release.yml` & `windows-mingw.yml` | 缺 `concurrency:` 与 `timeout-minutes:`：手动触发同 tag 会重叠刷 Release，单脚本卡死会一直占 runner | release 用 `concurrency: group: windows-release-${{ inputs.tag_name }} cancel-in-progress: false`；mingw 用 `cancel-in-progress: true`；每个 job 设 `timeout-minutes: 90` |
| S-B2 | `windows-release.yml:91-103` → `release-check.ps1:267` → `package-msix.ps1:357` | MSIX 签名密码以 `-MsixCertificatePassword` 明文 string 形参逐层透传到 `signtool /p`，进入 Win32 命令行，可被同会话 `Get-Process | Select CommandLine` / ETW 读取，且 strict-mode 抛错时 `$signArgs` 会被 dump | 改 `[SecureString]`，仅在调用 `signtool` 那一刻 `Marshal::SecureStringToBSTR`；不放进可被 `ConvertTo-Json` 序列化的数组 |
| S-B3 | `package-msix.ps1:202-207` | `[X509Certificate2]::new(...)` 之后未 `Dispose()`，崩溃路径下 CAPI/CNG 临时密钥容器残留 | `try/finally` 中 `Dispose()` |
| S-B4 | `signal-matrix-stress.ps1:102-127`、`clipboard-stress.ps1:212-237`（`Wait-FileContains`） | `Get-Content -Raw` 在 writer 句柄未关闭时抛 `IOException`，靠 100/200 ms sleep 循环到 12 s 超时；这正是 4 个 `Retry …` 提交的根因，未治本 | writer 改 `[IO.File]::WriteAllText` 立即关闭句柄并 `Sync`；reader 改 `Read-FileWithRetry`；超时参数化、上调到 30 s；理想方案：named pipe / FileSystemWatcher + ManualResetEventSlim |
| S-B5 | `completion-audit.ps1` ↔ `verify-release-artifacts.ps1:441-487` | hosted-CI green 没进 completion-audit 的 Required，导致 `RequireCompletionComplete` 通过、本地 `release-check.ps1` 通过、`verify-release-artifacts` 才挡，门禁两套并行容易脱钩 | 把 hosted-CI passed 直接列入 completion-audit 的 Required 项 |
| S-B6 | `configure.ac` 全平台 `AC_PROG_CXX` | `compat/win32-regex.cc` 仅在 mingw 需要 C++，但 `AC_PROG_CXX` 写在所有平台分支外，会让精简的 BSD/Solaris 容器没有 g++ 时直接配置失败，**破坏 POSIX 构建** | `case "$host_os" in *mingw*) AC_PROG_CXX ;; esac` |

---

## 二、关键 Major 问题（强烈建议合入前修）

### Win32 兼容层
- **M1** `win32-conpty.c:230-238` Job Object 创建未带 `JOB_OBJECT_LIMIT_BREAKAWAY_OK`/`SILENT_BREAKAWAY_OK`，子进程亦未 `CREATE_BREAKAWAY_FROM_JOB`。当 tmux 自身已被 PowerShell ISE / VSCode terminal / Windows Terminal / CI runner 的 job 包裹时（常态），`AssignProcessToJobObject` 会失败导致 `spawn failed`。建议 `IsProcessInJob` 探测后选择是否 breakaway。
- **M2** `win32-process.c:78-105` 通过 Toolhelp32 按 `ParentProcessID` 递归 kill 进程树：未做 `ProcessCreationTime` 校验，PID 复用会导致**误杀同会话其它进程**或递归成环。
- **M3** `win32-ipc.c:407-449` `accept` 后 `ioctlsocket FIONBIO=0` + 阻塞 5 s 等 token：恶意/异常客户端只要 connect 不发 token，server 主循环冻结 5 s，所有 client/window/pane 停摆。应改为 libevent 状态机（accept 后注册临时 ev_read，收齐 token 再升级正式 client）。
- **M4** `win32-ipc.c:235-247` endpoint 文件 `DeleteFileW → CreateFileW(CREATE_NEW)` 之间的 TOCTOU + 同 user 劫持窗口；server 崩溃后 endpoint 文件残留可能让下次 client 连到任意监听该端口的服务。`CREATE_ALWAYS` + 排他 share-mode + `FlushFileBuffers` + 双向 challenge token。
- **M5** `win32-stdio.c:289-310/312-330` `prepare_terminal` 部分失败时返回值与 `restore_terminal` 不一致，存在 mode 残留风险。
- **M7** `win32-environment.c:81` 空 environment block 在 `count==0` 时只有一个 `\0`，违反 Windows `lpEnvironment` 必须双 `\0` 结尾的契约；应改为返回 `NULL` 或 `total>=2`。

### tmux 核心层
- **K-M1** **大量重复实现**：`job.c`（+588）与 `spawn.c`（+321）、`cmd-pipe-pane.c`（+143）三处几乎逐字重写 `cwd_is_unc`、`cmd_pushd`、`make_environment`、`win32_get_shell` 等。应抽到 `compat/win32-spawn.c` 公共函数。
- **K-M2** `tmux.h` 接口签名变更：`proc_add_peer(int → imsg_fd_t)`、`server_create_socket` 返回类型、`server_client_create(int → imsg_fd_t)`，对外破坏 API。POSIX 下 `imsg_fd_t` 仍是 `int`，无 ABI 变化但有源码 API 变化，mainline 不接受。
- **K-M3** `struct window_pane` / `struct client` 直接内嵌 8+ 个 `win32_*` 字段（即便 `#ifdef _WIN32`）。上游传统是把平台字段下沉到 `osdep-*` 而非污染主结构体；建议改为 `void *plat_data`。
- **K-M4** `tmux-protocol.h` `PROTOCOL_VERSION 8 → 9`，新增 `MSG_STDIN` 与 `struct msg_resize`。该升级在没有上游协调的情况下擅自占用版本号，将导致此 fork 的 server/client 与 mainline 不能跨版本通信。
- **K-M5** `cmd-parse.y` `%token ERROR` → `PARSE_ERROR` 仅为绕开 Windows SDK 宏，**对 POSIX 也生效**。同样地 `environ` → `TMUX_ENVIRON` 宏化也污染 POSIX 路径。
- **K-M6** `cmd-new-session.c` POSIX 路径上 `c->fd != -1` 守卫位置变化，可能让 detached client 意外触发 `server_client_check_nested`。
- **K-M7** `tty-term.c` 内置 caps 表 + 自写 terminfo 解释器（~250 行）：放弃了系统 terminfo 数据库，与 ncurses `tparm` 在大写寄存器 P/g、负数 `%+`、`'\0'` 处理上有细节差异；并使用 3 个文件级 static 缓冲（`tty_term_win32_out/outlen/outsize`），`tty_term_string_*` 嵌套调用会失效。需要对照测试与生成脚本。
- **K-M8** `utf8.c` 自维护的 `utf8_win32_zero_width / utf8_win32_wide` 范围表数百行，没有给出 Unicode 版本号与生成脚本，后续维护无据可依。
- **K-M9** `file.c:35-43` `file_should_open_locally` 在 _WIN32 下硬编码"非 `-` 路径都本地打开"，绕过原 client/server 路由设计；当 server 与 client 工作目录不同时存在功能性差异。

### PowerShell / CI
- **S-M1** **18 个脚本逐字复制 `ConvertTo-WindowsArgument`（30 行 × 18）**；加上 `Invoke-Tmux` / `Wait-FileContains` / `Add-StepResult` 平均每脚本重复 80–150 行。建议抽 `windows/lib/{Tmux,Path,Sha,Git,CI}.psm1`，估计可减 3-4k 行。
- **S-M2** 18 处 `try { … } catch { }` 空 catch 包裹 `$process.Kill()`：把 `InvalidOperationException`（已退出）和 `Win32Exception`（access denied）一起吞掉。应只捕获前者。
- **S-M3** 编码不一致：`-Encoding ascii` 与 PS 5.1 默认 ANSI、PS 7 默认 UTF-8 混用；`Get-Content -Raw` 大量未指定 `-Encoding`。统一 `utf8NoBOM` 或 `[IO.File]::WriteAllText(..., [Text.UTF8Encoding]::new($false))`。
- **S-M4** `package-msix.ps1:69-101` `Find-WindowsKitTool` 通过 `Get-ChildItem` + `Sort-Object FullName -Descending` 找 `signtool.exe`，未做 `Get-AuthenticodeSignature` 校验 Microsoft 签名链；self-hosted runner / 企业 PATH 劫持场景下可被替换。
- **S-M5** `Out-File -FilePath $env:GITHUB_STEP_SUMMARY` 在 PS 5.1 默认 UTF-16 LE BOM，GitHub 解析的是 UTF-8，会乱码或破坏 markdown。改 `[IO.File]::AppendAllText` UTF-8 无 BOM。
- **S-M6** `linux-behavior-parity.ps1` 21 处散落 `Start-Sleep -Milliseconds 250/500/700/800/1000`：是 "Stabilize WSL behavior parity runner" 的根因，应改 `Wait-Until { … } -TimeoutMs N` 状态轮询。
- **S-M7** `install-portable.ps1` `Remove-DirectoryWithRetry` 退避总计 ~5.5 s，常见 indexer/AV 句柄持有需要更长；并在含只读文件时失败。
- **S-M8** `windows-release.yml` 顶层 permissions 缺 `id-token: none / packages: none` 最小化声明，未来易被默默打开 OIDC / 包推送。

### 文档与一致性
- **Q-M1** 4 个 `Retry …` 提交 + 5 处 `release-check.ps1` 反复改 + 7 个仅记录运行证据（hash/日期）的提交，应在合入前 squash。
- **Q-M2** `windows/PARITY-AUDIT.md` 第 99-103 行硬编码 portable zip / MSIX 的 SHA256；`VERIFY.md:10` 写 `cd D:\Users\jonaszchen\Documents\tmux`（开发者本机绝对路径）；`PARITY-AUDIT` 第 19 行 1 500 字单元格散文化。这些一次性数据应放进 `dist/` JSON，不进永久文档。
- **Q-M3** **`CHANGES` 完全无 Windows 端口提及**；**根 `README` 完全无 Windows 端口/构建说明**。上游 maintainer 看 CHANGES 是首选信号，必补。
- **Q-M4** PowerShell 脚本（23 个）无任何 ISC 许可证 header；`.github/workflows` 两个 yml 无许可声明。
- **Q-M5** `.gitignore` 已忽略 `dist/`、`tmux.exe`、`tmux-*.log`，但缺 `*.msix`、`*.appx`、`*.zip`、`*.sha256` 等兜底（开发者绕过 `dist/` 时易误提交）。

---

## 三、Minor / Nit（精选）

- **C-m11** `win32-stdio.c:84` console `ReadFile read==0` 时 `Sleep(1)` 后继续 loop——正常路径下 `read==0` 即 EOF，当前实现会让 server 100% CPU 不退出（边界 case，可提到 Major）。
- **C-m12** `win32-process.c:152` output 线程退出 `SD_SEND` shutdown，input 线程退出未对应 shutdown，对端无法收到 EOF。
- **C-m14** `win32-environment.c:79` `qsort` 用 `_wcsicmp`（受 LC_CTYPE 影响），土耳其 locale 下 `i/I` 比较异常；改 `CompareStringOrdinal(NORM_IGNORECASE)`。
- **C-m17** `osdep-windows.c:216-235` `ReadProcessMemory` 读 PEB 的 `UNICODE_STRING.Buffer`，未限制 `length` 上限，理论上恶意进程可让 calloc 申请 4 GB。
- **K-m** `spawn.c:459` 把 4 空格改 tab，混入大补丁污染 git blame，应剥离。
- **K-m** `cmd-source-file.c` 自实现 glob 在 `_WIN32` 下硬编码 `xasprintf("%s/%s", cwd, path)`（line 502-505）使用 `/` 分隔符，与 path_is_absolute 风格不一。
- **K-m** `enum keyc` 中部插入 `KEYC_BREAK`：偏移其后 `KEYC_F1..F12` 的枚举值，建议加到末尾。
- **S-m** `audit-command-surface.ps1` 的 minimums（CommandCount≥90 等）写死在 `verify-release-artifacts.ps1`，建议拆 `windows/release-baseline.json` 并打 hash。
- **S-m** `package-msix.ps1` `New-PngAsset` 每次 release 现画 PNG，依赖 `System.Drawing`（PS7/.NET 7 Linux 已不自带）；改预生成 PNG 入 git。
- **S-m** `probe-mingw.ps1` 命名误导（979 行实际承担 link/configure/build），改名 `compile-mingw.ps1`。

---

## 四、上游 mainline 可合入性评估

按 Nicholas Marriott 上游 tmux 的一贯口味（极简 POSIX-only、拒绝 `#ifdef` 蔓延），**当前 fork 不能直接合入 mainline**。即使技术上可考虑 Windows 端口，本次提交至少存在 4 道**红线问题**会被立即驳回：

1. 作者归属伪造（冒用 maintainer 署名）；
2. commit body 全空 + AI 占位邮箱（DCO/可审计性零）；
3. 巨型 ~28k 行起步提交 + 4 个 `Retry` + 多个仅记录运行证据的 commit（历史无法 review）；
4. 多处接口签名变更与 POSIX 路径行为变更未做隔离（`imsg_fd_t`、`utf8_wchar`、`PROTOCOL_VERSION`、`TMUX_ENVIRON`、`PARSE_ERROR`、`DRAG_SIZE`、`MSG_RESIZE` 顺序、`cmd-new-session` 守卫顺序）。

**推荐路径**：以独立 fork（如 `tmux-win32`）持续维护，仅把以下"准备性重构 PR 集"按子系统拆成 5–10 个独立小 PR 投上游：

| 可独立提交 mainline 的 patch | 出处 |
|---|---|
| `server_shutdown()` 抽出 | `server.c` |
| `file_write_open` / `file_read_open` 失败路径 `file_free` 修复 | `file.c:643,800` |
| `cmd-parse.y` `ps->condition=1` 仅在命中关键字时设置（bug fix） | `cmd-parse.y:1414-1442` |
| `window_pane_input_ready` helper 抽出 | `window.c` |
| `cmd_pipe_pane_close` helper 抽出 | `cmd-pipe-pane.c:51-67` |
| `cmd_server_access_deny` 改用 `uid+name` 而非 `struct passwd` | `cmd-server-access.c` |
| `path_is_absolute` / `path_is_directory` helper | `tmux.h` |
| `proc.c` `PEER_IN_CALLBACK` 优化（独立 patch + 上游沟通） | `proc.c:257-260` |

`compat/win32-*`、`tty-term.c` 内置 caps、PowerShell 脚本、release/CI 工作流长期留在 fork。

---

## 五、必须执行清单（按优先级）

### P0（红线，今日必修）
1. 重写 27 个新增 `compat/win32-*.{c,cc,h}` 与 `osdep-windows.c` 的版权署名为真实贡献者，保留 ISC 文本不变。
2. `git rebase -i origin/master` 重置全部 35 个提交的 author/email 为真实人类。
3. 给每个 commit 补完整 body（动机 / 设计 / 影响 / 测试方法）。
4. 将 `configure.ac` 的 `AC_PROG_CXX` 限定在 `*mingw*` host_os 分支，避免破坏 POSIX 构建。

### P1（合入前必修）
5. 修复 5 个兼容层 Blocker（C-B1..C-B5）：
   - `win32-stdio.c` 关闭顺序；
   - `win32-pty.c` Ctrl-Break 进程级竞态；
   - `win32-conpty.c` `ClosePseudoConsole` 超时处理；
   - `win32-process.c` IO 线程数据竞争；
   - `compat.h` 移除 POSIX 状态宏对 NTSTATUS 的复用。
6. 修复 3 个核心层 Blocker（K-B1..K-B3）：`enum SIZE→DRAG_SIZE` 局部化、`MSG_RESIZE` 顺序还原、`utf8_*` 公共签名保持 `wchar_t`。
7. 修复 6 个脚本 / CI Blocker（S-B1..S-B6）：concurrency + timeout + permissions 最小化、MSIX 签名密码 `SecureString` 化、`X509Certificate2` Dispose、ready-file `[IO.File]::WriteAllText`、completion-audit 收编 hosted-CI green。
8. Squash 4 个 `Retry` 提交、5 处 `release-check.ps1` 反复改、7 个 `Record … evidence` 类提交。
9. `CHANGES` 顶部新增 `CHANGES FROM 3.6a TO …` 块描述 Windows 端口主题；`README` 增加"Windows native build"小节链 `windows/PORTING.md` / `windows/RELEASE.md`。

### P2（强烈建议）
10. 修复 6 个核心 Major：`job.c`/`spawn.c`/`cmd-pipe-pane.c` 三处重复实现抽公共函数；`tmux.h` 接口签名回退；`struct window_pane/client` 平台字段改 `void *plat_data`；`PROTOCOL_VERSION` 决策与上游协调；`TMUX_ENVIRON`/`PARSE_ERROR` 改局部 `#ifdef` 处理；`cmd-new-session` 守卫顺序还原。
11. 修复 8 个兼容层 Major（M1..M5、M7、M9、M10）：Job Object breakaway、PID 复用环路、accept 非阻塞、prepare_terminal 部分失败、空 env block、handle 跨进程白名单。
12. 抽公共 PowerShell 模块 `windows/lib/Tmux.psm1` 等，估计可减 3-4k 行重复。
13. `PARITY-AUDIT/RELEASE/PORTING/VERIFY` 中所有硬编码 SHA256、日期戳、`D:\…` 绝对路径替换为占位符或迁出文档进 `dist/` JSON。
14. PowerShell 脚本统一加 ISC 许可证 header；`.gitignore` 补 `*.msix / *.appx / *.zip / *.sha256` 兜底。
15. `tty-term.c` 自写 terminfo 解释器加对照测试与生成脚本；`utf8.c` 范围表标注 Unicode 版本与生成器。

---

## 六、结论与建议

- **当前是否可 push**：否，至少在修完 P0（版权 + author + commit body + `AC_PROG_CXX` 限定）之前不应推送到任何受信任远端，否则一旦泄露到 mirror 即对真实自然人作者形成不可挽回的署名争议。
- **当前是否可作为 PR 提交 tmux 上游**：否，需要按"五.P0 + P1 + P2"全部完成后，再按"四.推荐路径"拆分为 8 个准备性重构 PR 与独立 Windows-port RFC 走上游设计协商。
- **当前是否可作为独立 fork 发布**：修完 P0、C-B1..B5、K-B1..B3、S-B1..B6（最低限度的红线 + 稳定性 Blocker）即可发布预览版；S-B4 / S-B6 等可继续在后续小版本里收口。
- **整体观察**：变更显示出明显的"AI 一两天集中产出 + 反复事后修补"的痕迹（35 提交全部 Codex / 2026-05-18~19 / 出现 4 个 `Retry`），代码工程意识在表层（错误处理、JSON 摘要、双重门禁）相对到位，但底层并发与生命周期管理（IO 线程、ConPTY 关闭、进程级控制台抢占、Job Object 嵌套）存在多处不可在单次冒烟里复现的偶发 bug。建议在合入或发布前**专门跑一组 stress 测试**：连续 attach/detach 1 000 次、Ctrl-C 注入风暴、含 `0xC0000005` 退出的子进程、被 PowerShell job 包裹时 spawn、被 indexer/AV 持有句柄时 install-portable cleanup。

---

## 附录 A：35 个未推送提交一览

```
8d23b779 Add MSIX signing smoke test                      (2026-05-19)
b1075d8e Retry clipboard stress ready-file reads          ← Retry / fixup
b0b0f0a8 Aggregate production readiness blockers          ← 事后补救
59e935a1 Retry signal matrix ready-file reads             ← Retry / fixup
55f84ccb Preflight MSIX signing certificate usability
62d55f7c Report production signing readiness gaps
1ba66f2b Retry portable install cleanup on Windows        ← Retry / fixup
46e00e7b Diagnose unpublished hosted CI branches
665cb9e6 Stabilize WSL behavior parity runner
53f9f516 Audit Linux parity binary traceability
1cca7ede Tie Linux parity evidence to tested tmux binary  ← 仅 hash 证据
4a2af35f Record release gate source commit evidence       ← 仅 hash 证据
0ccc211f Record fuller Windows release gate command evidence ← 仅 hash 证据
0f0f02ed Document production readiness gate in release notes
aacd238b Add Windows production release readiness gate
2cb94e12 Fail early for missing MSIX signing certificates
d5305cac Audit local code signing certificate readiness
9632e0dc Preserve hosted CI diagnostics in completion audit
dfb40235 Include hosted CI diagnostics in release notes
240aff62 Record local workflow evidence in hosted CI audit ← 仅 hash 证据
d7cc9723 Improve hosted CI audit diagnostics
6aa93976 Verify required Windows release steps
3dd1b271 Tighten Windows release artifact verification
9ed50c53 Record fresh Windows release gate hashes         ← 仅 hash 证据
464dd136 Avoid PowerShell Ctrl-Break WER in smoke
3514aa85 Require clipboard stress in Windows release gate
6f2970b4 Require clipboard availability in release stress
f37eff57 Cover choice.exe in Windows signal matrix
9e8d69f5 Wire clipboard stress into Windows release check
50d8dc1e Add Windows clipboard contention stress
b8480659 Audit Linux default option parity on Windows
41dd509b Record extended Windows console attach soak evidence ← 仅 hash 证据
11dbb8e6 Expand Windows Linux behavior parity coverage
a16c6fe2 Update Windows parity audit release status        ← 文档状态更新
407645d1 Add native Windows port and release checks       ← 巨型起步 ~28k 行
```

## 附录 B：评审来源

- **lead**: 综合 + 报告撰写
- **reviewer-compat**: 5 Blocker / 10 Major / 19 Minor / Nit（compat 层 6.0/10）
- **reviewer-core**: 2 Blocker（含 1 接口签名级）/ 25 Major / 30 Minor（核心层 6.5/10）
- **reviewer-scripts**: 6 Blocker / 10 Major / 10 Minor（脚本+CI 6.0/10）
- **reviewer-quality**: 提交 2/10 · 文档可维护性 4/10 · 版权 1/10 · 整体一致性 7/10 · mainline 可合入度 1/10 · fork 可发布度 6/10
