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

#ifndef TMUX_WIN32_DAEMON_H
#define TMUX_WIN32_DAEMON_H

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define WIN32_DAEMON_COOKIE_SIZE	16
#define WIN32_DAEMON_HANDSHAKE_TIMEOUT_MS 1000

/*
 * The role distinguishes the two ends of a daemon channel. The same
 * struct win32_daemon_handle backs either side, but the role decides which
 * side of the named pipe (server-end vs client-end) the handle owns.
 */
enum win32_daemon_role {
	WIN32_DAEMON_ROLE_NONE = 0,
	WIN32_DAEMON_ROLE_CLIENT = 1,
	WIN32_DAEMON_ROLE_SERVER = 2
};

/*
 * Lifecycle state for the control channel. Used purely for log/diagnostic
 * sanity checks; no caller is expected to switch on it directly.
 */
enum win32_daemon_state {
	WIN32_DAEMON_STATE_INIT = 0,
	WIN32_DAEMON_STATE_LISTENING = 1,
	WIN32_DAEMON_STATE_HANDSHAKING = 2,
	WIN32_DAEMON_STATE_READY = 3,
	WIN32_DAEMON_STATE_CLOSED = 4
};

/*
 * Backing structure for a daemon control channel. A single handle owns
 * either the server-end (created by the spawning client) or the client-end
 * (after the server has been launched and the client reconnects).
 *
 * All public API funcs accept a caller-allocated handle; lifetime is
 * controlled by win32_daemon_close().
 */
struct win32_daemon_handle {
	HANDLE		pipe_ctl;
	HANDLE		pipe_evt;
	HANDLE		child_process;
	DWORD		child_pid;
	wchar_t		*endpoint_path;
	wchar_t		*pipe_ctl_name;
	wchar_t		*pipe_evt_name;
	int		role;
	int		state;
	SRWLOCK		lock;
	unsigned char	cookie[WIN32_DAEMON_COOKIE_SIZE];
};

/*
 * Public API. All funcs return 0 on success, -1 on failure with the
 * Windows last-error preserved (translate via win32_errno_from_lasterror
 * before crossing into POSIX-style call sites).
 *
 * win32_daemon_spawn_server	create -ctl/-evt named pipes, write the
 *				endpoint index file, and fork a detached
 *				server via CreateProcessW; performs the
 *				3-way handshake.
 *
 * win32_daemon_connect		read the endpoint index file, open the
 *				client-end of the -ctl pipe, send CONFIRM
 *				cookie. Used when an existing server is
 *				already up.
 *
 * win32_daemon_send/recv	write/read message-mode frames over the
 *				control pipe; bytes are passed through
 *				untouched (PROTOCOL_VERSION=8 unchanged).
 *
 * win32_daemon_close		release every handle, wait for the child
 *				to exit gracefully if applicable, and
 *				delete the endpoint index file.
 */
int	win32_daemon_spawn_server(struct win32_daemon_handle *,
	    const char *socket_name);
int	win32_daemon_connect(struct win32_daemon_handle *,
	    const char *socket_name);
int	win32_daemon_send(struct win32_daemon_handle *, const void *buf,
	    size_t len);
int	win32_daemon_recv(struct win32_daemon_handle *, void *buf,
	    size_t len);
void	win32_daemon_close(struct win32_daemon_handle *);

#endif /* _WIN32 */

#endif /* TMUX_WIN32_DAEMON_H */
