/*
 * Copyright (c) 2026 jonaszchen <jonaszchen@gmail.com>
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

#ifndef TMUX_WIN32_IPC_H
#define TMUX_WIN32_IPC_H

#ifdef _WIN32

#include <stdint.h>
#include <wchar.h>

#define WIN32_IPC_TOKEN_SIZE 32
#define WIN32_IPC_HANDSHAKE_TIMEOUT_SEC 5

struct win32_ipc_listener;

/*
 * Callback invoked when a pending handshake completes (token valid)
 * or fails (token invalid, timeout, or error).
 *
 * On success, socket_out points to the validated nonblocking socket.
 * On failure, socket_out is INVALID_SOCKET and the handshake object
 * has already been cleaned up.
 */
typedef void (*win32_ipc_handshake_cb)(uintptr_t socket_out, void *arg);

struct win32_ipc_listener {
	uintptr_t	 socket;
	wchar_t		*path;
	unsigned char	 token[WIN32_IPC_TOKEN_SIZE];
	unsigned short	 port;
};

int	win32_ipc_listen(const char *, struct win32_ipc_listener *);
int	win32_ipc_accept(struct win32_ipc_listener *, uintptr_t *);
void	win32_ipc_accept_nonblocking(struct win32_ipc_listener *,
	    win32_ipc_handshake_cb, void *);
int	win32_ipc_connect(const char *, uintptr_t *);
void	win32_ipc_listener_close(struct win32_ipc_listener *);

#endif

#endif
