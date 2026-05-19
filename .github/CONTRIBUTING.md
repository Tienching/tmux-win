## What should I do before opening an issue?

Before opening an issue, please ensure that:

- Your problem is a specific problem or question or suggestion, not a general
  complaint.

- You are reporting a Windows-native tmux issue for this repository:
  `https://github.com/Tienching/tmux-win`.

- You can reproduce the problem with the latest `tmux-win` build from this
  repository.

- Your question or issue is not covered in `README`, `windows/VERIFY.md`,
  `windows/PORTING.md`, or the local `tmux.1` manpage.

- Your problem is not mentioned in the local `CHANGES` file.

- Nobody else has opened the same issue recently.

## What should I include in an issue?

Please include the output of:

~~~powershell
.\tmux.exe -V
$PSVersionTable.PSVersion
[System.Environment]::OSVersion.VersionString
~~~

Also include:

- Your Windows version and terminal host, such as Windows Terminal, conhost,
  PowerShell, cmd, or another shell.

- A brief description of the problem with steps to reproduce.

- A minimal tmux config, if you can't reproduce without a config.

- The exact `tmux.exe` path under test. If you use a portable package, include
  whether it came from `dist\tmux-win32-portable`.

- Logs from tmux (see below). Please attach logs to the issue directly rather
  than using a download site or pastebin. Put in a zip file if necessary.

- At most one or two screenshots, if helpful.

## How do I test without a .tmux.conf?

Run a separate tmux server with `-f/dev/null` to skip loading `.tmux.conf`:

~~~powershell
.\tmux.exe -Ltest kill-server
.\tmux.exe -Ltest -f/dev/null new
~~~

## How do I get logs from tmux?

Add `-vv` to tmux to create three log files in the current directory. If you can
reproduce without a configuration file:

~~~powershell
.\tmux.exe -Ltest kill-server
.\tmux.exe -vv -Ltest -f/dev/null new
~~~

Or if you need your configuration:

~~~powershell
.\tmux.exe kill-server
.\tmux.exe -vv new
~~~

The log files are:

- `tmux-server*.log`: server log file.

- `tmux-client*.log`: client log file.

- `tmux-out*.log`: output log file.

Please attach the log files to your issue.

## What does it mean if an issue is closed?

All it means is that work on the issue is not planned for the near future. See
the issue's comments to find out if contributions would be welcome.
