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
	/*
	 * Subsequent layers (T-007 spawn, T-008 endpoint, T-009 handshake)
	 * are added by their respective commits; until then, returning here
	 * is enough to satisfy T-006 acceptance: both pipes exist with
	 * ACL/inheritance flags as required.
	 */
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return (-1);
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
