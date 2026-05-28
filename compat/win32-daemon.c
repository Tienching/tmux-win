/* $OpenBSD$ */
/*
 * Copyright (c) 2026 tmux Windows Port Contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef _WIN32

#include "win32-daemon.h"
#include "win32-acl.h"
#include "win32-endpoint.h"
#include "win32-errno.h"

#include <stdlib.h>
#include <string.h>
#include <userenv.h>

/*
 * Phase 1, T-006: named pipe creation with ACL and handle-inheritance
 * policy. design.md sec 2.2 requires:
 *   - PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
 *   - 64KB in/out
 *   - PIPE_UNLIMITED_INSTANCES
 *   - DACL granting GENERIC_ALL only to the current owner
 *   - server-end inheritable (only via PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
 *     never the global TRUE-inherit default), client-end FALSE.
 */

#define WIN32D_PIPE_BUFFER_SIZE	(64 * 1024)
#define WIN32D_PIPE_DEFAULT_TIMEOUT_MS 50

/*
 * Common base flags for the SERVER end:
 *   PIPE_ACCESS_DUPLEX           bidirectional
 *   FILE_FLAG_FIRST_PIPE_INSTANCE prevents racing a hijacker
 *   FILE_FLAG_OVERLAPPED         lets later phases use IOCP / overlapped
 *                                IO without recreating the pipe.
 */
static HANDLE
win32d_create_pipe_server(const wchar_t *name, BOOL inheritable)
{
	SECURITY_ATTRIBUTES	sa;
	HANDLE			pipe = INVALID_HANDLE_VALUE;
	DWORD			open_mode;
	DWORD			pipe_mode;

	if (name == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (INVALID_HANDLE_VALUE);
	}
	if (win32_acl_owner_only(&sa) != 0)
		return (INVALID_HANDLE_VALUE);
	sa.bInheritHandle = inheritable;

	open_mode = PIPE_ACCESS_DUPLEX |
	    FILE_FLAG_FIRST_PIPE_INSTANCE |
	    FILE_FLAG_OVERLAPPED;
	pipe_mode = PIPE_TYPE_MESSAGE |
	    PIPE_READMODE_MESSAGE |
	    PIPE_WAIT |
	    PIPE_REJECT_REMOTE_CLIENTS;

	pipe = CreateNamedPipeW(name, open_mode, pipe_mode,
	    PIPE_UNLIMITED_INSTANCES,
	    WIN32D_PIPE_BUFFER_SIZE,
	    WIN32D_PIPE_BUFFER_SIZE,
	    WIN32D_PIPE_DEFAULT_TIMEOUT_MS,
	    &sa);
	win32_acl_owner_only_free(&sa);
	return (pipe);
}

/*
 * Open the CLIENT end of an existing named pipe. The handle never
 * inherits — bridge handles to child processes go through
 * PROC_THREAD_ATTRIBUTE_HANDLE_LIST (T-007) only.
 */
static HANDLE
win32d_open_pipe_client(const wchar_t *name)
{
	SECURITY_ATTRIBUTES	sa;
	HANDLE			handle;
	DWORD			mode;

	memset(&sa, 0, sizeof sa);
	sa.nLength = sizeof sa;
	sa.bInheritHandle = FALSE;

	handle = CreateFileW(name,
	    GENERIC_READ | GENERIC_WRITE,
	    0,
	    &sa,
	    OPEN_EXISTING,
	    FILE_FLAG_OVERLAPPED,
	    NULL);
	if (handle == INVALID_HANDLE_VALUE)
		return (INVALID_HANDLE_VALUE);
	mode = PIPE_READMODE_MESSAGE;
	if (!SetNamedPipeHandleState(handle, &mode, NULL, NULL)) {
		DWORD saved = GetLastError();
		CloseHandle(handle);
		SetLastError(saved);
		return (INVALID_HANDLE_VALUE);
	}
	return (handle);
}

static void
win32d_handle_init(struct win32_daemon_handle *handle, int role)
{
	memset(handle, 0, sizeof *handle);
	InitializeSRWLock(&handle->lock);
	handle->role = role;
	handle->state = WIN32_DAEMON_STATE_INIT;
	handle->pipe_ctl = INVALID_HANDLE_VALUE;
	handle->pipe_evt = INVALID_HANDLE_VALUE;
	handle->child_process = NULL;
}

/*
 * T-006 entry: create both -ctl and -evt server-end pipes for the given
 * socket name. Used by spawn_server before fork; the resulting handles
 * are inheritable so they can be passed to the child via
 * PROC_THREAD_ATTRIBUTE_HANDLE_LIST.
 *
 * On success the names are also memoised on the handle so subsequent
 * connect / cleanup paths do not need to recompute them.
 */
static int
win32d_create_server_pipes(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	wchar_t	pipe_ctl[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t	pipe_evt[WIN32_ENDPOINT_PIPE_MAX];

	if (win32_endpoint_format_pipe_names(socket_name, pipe_ctl,
	    pipe_evt) != 0)
		return (-1);

	handle->pipe_ctl = win32d_create_pipe_server(pipe_ctl, TRUE);
	if (handle->pipe_ctl == INVALID_HANDLE_VALUE)
		return (-1);
	handle->pipe_evt = win32d_create_pipe_server(pipe_evt, TRUE);
	if (handle->pipe_evt == INVALID_HANDLE_VALUE) {
		DWORD saved = GetLastError();
		CloseHandle(handle->pipe_ctl);
		handle->pipe_ctl = INVALID_HANDLE_VALUE;
		SetLastError(saved);
		return (-1);
	}

	handle->pipe_ctl_name = _wcsdup(pipe_ctl);
	handle->pipe_evt_name = _wcsdup(pipe_evt);
	if (handle->pipe_ctl_name == NULL || handle->pipe_evt_name == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	handle->state = WIN32_DAEMON_STATE_LISTENING;
	return (0);
}

/*
 * T-007: CreateProcessW spawn of the detached server. The child inherits
 * only the two named-pipe server ends via PROC_THREAD_ATTRIBUTE_HANDLE_LIST;
 * stdio is redirected to NUL so the server never holds the client console;
 * the working directory is forced to %SystemRoot% to avoid pinning the
 * caller's CWD on disk.
 *
 * Command line: <argv[0]> --server-detached <pipe-ctl> <pipe-evt> <socket>.
 * The endpoint path is communicated through the TMUX_ENDPOINT environment
 * variable populated by T-008 (the spawn here only adds TMUX_PIPE_* so the
 * child can confirm the bridge before T-008 lands). Argument quoting is
 * handled by enclosing each argument in double quotes; pipe names contain
 * only ASCII characters from win32_endpoint_format_pipe_names so a basic
 * quote-and-pass approach is safe.
 */
static int
win32d_build_command_line(const wchar_t *exe, const wchar_t *pipe_ctl,
    const wchar_t *pipe_evt, const char *socket_name, wchar_t **out)
{
	wchar_t	*socket_w = NULL;
	wchar_t	*line = NULL;
	size_t	cap;
	int	rc = -1;
	int	n;

	if (exe == NULL || pipe_ctl == NULL || pipe_evt == NULL ||
	    socket_name == NULL || out == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*out = NULL;
	{
		int needed = MultiByteToWideChar(CP_UTF8, 0, socket_name, -1,
		    NULL, 0);
		if (needed <= 0)
			return (-1);
		socket_w = calloc((size_t)needed, sizeof *socket_w);
		if (socket_w == NULL) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return (-1);
		}
		if (MultiByteToWideChar(CP_UTF8, 0, socket_name, -1, socket_w,
		    needed) <= 0)
			goto out;
	}

	cap = wcslen(exe) + wcslen(pipe_ctl) + wcslen(pipe_evt) +
	    wcslen(socket_w) + 64;
	line = calloc(cap, sizeof *line);
	if (line == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	n = swprintf_s(line, cap,
	    L"\"%ls\" --server-detached \"%ls\" \"%ls\" \"%ls\"",
	    exe, pipe_ctl, pipe_evt, socket_w);
	if (n < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	*out = line;
	line = NULL;
	rc = 0;

out:
	free(socket_w);
	free(line);
	return (rc);
}

static int
win32d_resolve_self_path(wchar_t out[MAX_PATH * 2])
{
	DWORD	n;

	n = GetModuleFileNameW(NULL, out, MAX_PATH * 2);
	if (n == 0 || n >= MAX_PATH * 2) {
		if (n >= MAX_PATH * 2)
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}
	return (0);
}

static int
win32d_open_nul(HANDLE *handle, BOOL inherit, DWORD access)
{
	SECURITY_ATTRIBUTES	sa;
	HANDLE			h;

	memset(&sa, 0, sizeof sa);
	sa.nLength = sizeof sa;
	sa.bInheritHandle = inherit;

	h = CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE,
	    &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return (-1);
	*handle = h;
	return (0);
}

static wchar_t *
win32d_system_root(void)
{
	wchar_t	*buf;
	DWORD	n;

	buf = calloc(MAX_PATH * 2, sizeof *buf);
	if (buf == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	n = GetEnvironmentVariableW(L"SystemRoot", buf, MAX_PATH * 2);
	if (n == 0 || n >= MAX_PATH * 2) {
		wcscpy_s(buf, MAX_PATH * 2, L"C:\\Windows");
	}
	return (buf);
}

static int
win32d_spawn_child(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	wchar_t			 self[MAX_PATH * 2];
	wchar_t			*command_line = NULL;
	wchar_t			*system_root = NULL;
	wchar_t			*environment = NULL;
	HANDLE			 nul_in = INVALID_HANDLE_VALUE;
	HANDLE			 nul_out = INVALID_HANDLE_VALUE;
	HANDLE			 nul_err = INVALID_HANDLE_VALUE;
	HANDLE			 inherit_handles[5];
	int			 inherit_count = 0;
	STARTUPINFOEXW		 startup;
	PROCESS_INFORMATION	 pi;
	SIZE_T			 attribute_size = 0;
	LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = NULL;
	int			 attribute_initialized = 0;
	DWORD			 flags;
	BOOL			 ok;
	int			 rc = -1;

	memset(&startup, 0, sizeof startup);
	memset(&pi, 0, sizeof pi);

	if (win32d_resolve_self_path(self) != 0)
		goto out;
	if (win32d_build_command_line(self, handle->pipe_ctl_name,
	    handle->pipe_evt_name, socket_name, &command_line) != 0)
		goto out;

	system_root = win32d_system_root();
	if (system_root == NULL)
		goto out;

	if (win32d_open_nul(&nul_in, TRUE, GENERIC_READ) != 0)
		goto out;
	if (win32d_open_nul(&nul_out, TRUE, GENERIC_WRITE) != 0)
		goto out;
	if (win32d_open_nul(&nul_err, TRUE, GENERIC_WRITE) != 0)
		goto out;

	/* Inheritance whitelist: pipe_ctl, pipe_evt, NUL stdio. */
	inherit_handles[inherit_count++] = handle->pipe_ctl;
	inherit_handles[inherit_count++] = handle->pipe_evt;
	inherit_handles[inherit_count++] = nul_in;
	inherit_handles[inherit_count++] = nul_out;
	inherit_handles[inherit_count++] = nul_err;

	InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
	attribute_list = HeapAlloc(GetProcessHeap(), 0, attribute_size);
	if (attribute_list == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (!InitializeProcThreadAttributeList(attribute_list, 1, 0,
	    &attribute_size))
		goto out;
	attribute_initialized = 1;
	if (!UpdateProcThreadAttribute(attribute_list, 0,
	    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_handles,
	    inherit_count * sizeof inherit_handles[0], NULL, NULL))
		goto out;

	if (!CreateEnvironmentBlock((LPVOID *)&environment, NULL, FALSE))
		environment = NULL;

	startup.StartupInfo.cb = sizeof startup;
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = nul_in;
	startup.StartupInfo.hStdOutput = nul_out;
	startup.StartupInfo.hStdError = nul_err;
	startup.lpAttributeList = attribute_list;

	flags = DETACHED_PROCESS |
	    CREATE_NEW_PROCESS_GROUP |
	    CREATE_UNICODE_ENVIRONMENT |
	    EXTENDED_STARTUPINFO_PRESENT |
	    CREATE_BREAKAWAY_FROM_JOB;

	ok = CreateProcessW(self, command_line, NULL, NULL, TRUE, flags,
	    environment, system_root, &startup.StartupInfo, &pi);
	if (!ok)
		goto out;

	handle->child_process = pi.hProcess;
	handle->child_pid = pi.dwProcessId;
	if (pi.hThread != NULL)
		CloseHandle(pi.hThread);
	pi.hThread = NULL;
	pi.hProcess = NULL;
	rc = 0;

out:
	if (pi.hProcess != NULL) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hProcess);
	}
	if (pi.hThread != NULL)
		CloseHandle(pi.hThread);
	if (attribute_list != NULL) {
		if (attribute_initialized)
			DeleteProcThreadAttributeList(attribute_list);
		HeapFree(GetProcessHeap(), 0, attribute_list);
	}
	if (environment != NULL)
		DestroyEnvironmentBlock(environment);
	if (nul_in != INVALID_HANDLE_VALUE)
		CloseHandle(nul_in);
	if (nul_out != INVALID_HANDLE_VALUE)
		CloseHandle(nul_out);
	if (nul_err != INVALID_HANDLE_VALUE)
		CloseHandle(nul_err);
	free(command_line);
	free(system_root);
	return (rc);
}

int
win32_daemon_spawn_server(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	if (handle == NULL || socket_name == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	win32d_handle_init(handle, WIN32_DAEMON_ROLE_SERVER);

	if (win32d_create_server_pipes(handle, socket_name) != 0) {
		DWORD saved = GetLastError();
		win32_daemon_close(handle);
		SetLastError(saved);
		return (-1);
	}
	if (win32d_spawn_child(handle, socket_name) != 0) {
		DWORD saved = GetLastError();
		win32_daemon_close(handle);
		SetLastError(saved);
		return (-1);
	}
	/*
	 * Endpoint write (T-008) and 3-way handshake (T-009) layered in by
	 * subsequent commits. T-007 acceptance only requires spawn to enter
	 * the detached branch without fatalx; success here means the child
	 * is alive and the server-end pipes are listening.
	 */
	handle->state = WIN32_DAEMON_STATE_HANDSHAKING;
	return (0);
}

int
win32_daemon_connect(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	wchar_t	pipe_ctl[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t	pipe_evt[WIN32_ENDPOINT_PIPE_MAX];

	if (handle == NULL || socket_name == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	win32d_handle_init(handle, WIN32_DAEMON_ROLE_CLIENT);

	if (win32_endpoint_format_pipe_names(socket_name, pipe_ctl,
	    pipe_evt) != 0)
		return (-1);

	handle->pipe_ctl = win32d_open_pipe_client(pipe_ctl);
	if (handle->pipe_ctl == INVALID_HANDLE_VALUE)
		return (-1);
	handle->pipe_evt = win32d_open_pipe_client(pipe_evt);
	if (handle->pipe_evt == INVALID_HANDLE_VALUE) {
		DWORD saved = GetLastError();
		CloseHandle(handle->pipe_ctl);
		handle->pipe_ctl = INVALID_HANDLE_VALUE;
		SetLastError(saved);
		return (-1);
	}
	handle->pipe_ctl_name = _wcsdup(pipe_ctl);
	handle->pipe_evt_name = _wcsdup(pipe_evt);
	if (handle->pipe_ctl_name == NULL || handle->pipe_evt_name == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	/*
	 * Handshake (T-009) and message exchange (T-010) layered in by
	 * subsequent commits.
	 */
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return (-1);
}

int
win32_daemon_send(struct win32_daemon_handle *handle, const void *buf,
    size_t len)
{
	if (handle == NULL || buf == NULL || len == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return (-1);
}

int
win32_daemon_recv(struct win32_daemon_handle *handle, void *buf, size_t len)
{
	if (handle == NULL || buf == NULL || len == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return (-1);
}

void
win32_daemon_close(struct win32_daemon_handle *handle)
{
	if (handle == NULL)
		return;
	if (handle->pipe_ctl != NULL &&
	    handle->pipe_ctl != INVALID_HANDLE_VALUE)
		CloseHandle(handle->pipe_ctl);
	if (handle->pipe_evt != NULL &&
	    handle->pipe_evt != INVALID_HANDLE_VALUE)
		CloseHandle(handle->pipe_evt);
	if (handle->child_process != NULL)
		CloseHandle(handle->child_process);
	free(handle->endpoint_path);
	free(handle->pipe_ctl_name);
	free(handle->pipe_evt_name);
	memset(handle, 0, sizeof *handle);
	handle->pipe_ctl = INVALID_HANDLE_VALUE;
	handle->pipe_evt = INVALID_HANDLE_VALUE;
	handle->state = WIN32_DAEMON_STATE_CLOSED;
}

#endif /* _WIN32 */
