/*
 * Copyright (c) 2026 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "win32-socketpair.h"

#ifndef __unused
#ifdef __GNUC__
#define __unused __attribute__((unused))
#else
#define __unused
#endif
#endif

static INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK
win32_winsock_init_once(__unused PINIT_ONCE once, __unused PVOID parameter,
    __unused PVOID *context)
{
	WSADATA	data;

	return (WSAStartup(MAKEWORD(2, 2), &data) == 0);
}

static int
win32_winsock_init(void)
{
	if (!InitOnceExecuteOnce(&winsock_once, win32_winsock_init_once, NULL,
	    NULL))
		return (-1);
	return (0);
}

static void
win32_socket_close_ptr(SOCKET *s)
{
	if (*s != INVALID_SOCKET) {
		closesocket(*s);
		*s = INVALID_SOCKET;
	}
}

int
win32_socketpair(uintptr_t sockets[2])
{
	struct sockaddr_in	 addr;
	SOCKET			 listener = INVALID_SOCKET;
	SOCKET			 left = INVALID_SOCKET, right = INVALID_SOCKET;
	int			 len, on = 1;

	sockets[0] = sockets[1] = (uintptr_t)INVALID_SOCKET;
	if (win32_winsock_init() != 0)
		return (-1);

	listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
		goto fail;

	setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
	    (const char *)&on, sizeof on);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(listener, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR)
		goto fail;
	if (listen(listener, 1) == SOCKET_ERROR)
		goto fail;

	len = sizeof addr;
	if (getsockname(listener, (struct sockaddr *)&addr, &len) == SOCKET_ERROR)
		goto fail;

	left = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (left == INVALID_SOCKET)
		goto fail;
	if (connect(left, (struct sockaddr *)&addr, sizeof addr) == SOCKET_ERROR)
		goto fail;

	right = accept(listener, NULL, NULL);
	if (right == INVALID_SOCKET)
		goto fail;

	closesocket(listener);
	sockets[0] = (uintptr_t)left;
	sockets[1] = (uintptr_t)right;
	return (0);

fail:
	win32_socket_close_ptr(&right);
	win32_socket_close_ptr(&left);
	win32_socket_close_ptr(&listener);
	return (-1);
}

int
win32_socket_set_blocking(uintptr_t socket, int blocking)
{
	u_long	mode;

	mode = blocking ? 0 : 1;
	if (ioctlsocket((SOCKET)socket, FIONBIO, &mode) == SOCKET_ERROR)
		return (-1);
	return (0);
}

int
win32_socket_pending(uintptr_t socket, unsigned long *pending)
{
	u_long	n = 0;

	if (pending == NULL) {
		WSASetLastError(WSAEINVAL);
		return (-1);
	}
	if (ioctlsocket((SOCKET)socket, FIONREAD, &n) == SOCKET_ERROR)
		return (-1);
	*pending = n;
	return (0);
}

int
win32_socket_shutdown(uintptr_t socket, int write_only)
{
	if (shutdown((SOCKET)socket, write_only ? SD_SEND : SD_BOTH) ==
	    SOCKET_ERROR)
		return (-1);
	return (0);
}

int
win32_socket_close(uintptr_t socket)
{
	if (closesocket((SOCKET)socket) == SOCKET_ERROR)
		return (-1);
	return (0);
}

#endif
