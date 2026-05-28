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

#include <stdlib.h>
#include <string.h>

/*
 * T-001 skeleton: only the 5 public symbols are exported; concrete pipe /
 * spawn / handshake logic is layered in by T-006~T-010. Keeping the stubs
 * separate makes the dependency graph in tasks.md (T-002~T-005 build on
 * T-001 but do not require the spawn flow yet) trivially satisfiable.
 */

int
win32_daemon_spawn_server(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	if (handle == NULL || socket_name == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(handle, 0, sizeof *handle);
	InitializeSRWLock(&handle->lock);
	handle->role = WIN32_DAEMON_ROLE_SERVER;
	handle->state = WIN32_DAEMON_STATE_INIT;
	handle->pipe_ctl = INVALID_HANDLE_VALUE;
	handle->pipe_evt = INVALID_HANDLE_VALUE;
	handle->child_process = NULL;
	SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
	return (-1);
}

int
win32_daemon_connect(struct win32_daemon_handle *handle,
    const char *socket_name)
{
	if (handle == NULL || socket_name == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(handle, 0, sizeof *handle);
	InitializeSRWLock(&handle->lock);
	handle->role = WIN32_DAEMON_ROLE_CLIENT;
	handle->state = WIN32_DAEMON_STATE_INIT;
	handle->pipe_ctl = INVALID_HANDLE_VALUE;
	handle->pipe_evt = INVALID_HANDLE_VALUE;
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
	if (handle->pipe_ctl != NULL && handle->pipe_ctl != INVALID_HANDLE_VALUE)
		CloseHandle(handle->pipe_ctl);
	if (handle->pipe_evt != NULL && handle->pipe_evt != INVALID_HANDLE_VALUE)
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
