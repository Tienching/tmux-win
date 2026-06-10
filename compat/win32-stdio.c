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

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "win32-socketpair.h"
#include "win32-stdio.h"

#define WIN32_STDIO_BUFFER 8192
#define WIN32_STDIO_CLOSE_TIMEOUT_MS 5000

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

/*
 * Immutable thread argument structures.  Workers copy the fields they
 * need and free the args struct immediately, so they never access the
 * parent bridge struct during their lifetime.
 */
struct win32_stdio_input_args {
	int	 input_fd;
	SOCKET	 input_bridge_socket;
};

struct win32_stdio_output_args {
	int				 output_fd;
	SOCKET				 output_bridge_socket;
	int				 output_console;
	struct win32_stdio_cursor	*cursor;  /* shared; not owned */
};

static void
win32_stdio_close_fd(int *fd)
{
	if (*fd != -1) {
		_close(*fd);
		*fd = -1;
	}
}

static int	win32_stdio_fd_handle(int, HANDLE *);

static void
win32_stdio_close_socket(uintptr_t *socket)
{
	if (*socket != (uintptr_t)INVALID_SOCKET) {
		win32_socket_shutdown(*socket, 0);
		win32_socket_close(*socket);
		*socket = (uintptr_t)INVALID_SOCKET;
	}
}

/*
 * Apply a pending cursor position to the console output.
 * Safe to call from the output worker because it only accesses
 * the shared cursor struct, not the bridge.
 */
static void
win32_stdio_apply_console_cursor(struct win32_stdio_cursor *cursor)
{
	CONSOLE_SCREEN_BUFFER_INFO	 info;
	COORD				 pos;
	SHORT				 maxx, maxy;

	if (cursor == NULL || !cursor->pending_valid ||
	    cursor->console_handle == NULL)
		return;

	EnterCriticalSection(&cursor->lock);
	if (!cursor->pending_valid) {
		LeaveCriticalSection(&cursor->lock);
		return;
	}
	pos.X = cursor->pending_x;
	pos.Y = cursor->pending_y;
	cursor->pending_valid = 0;
	LeaveCriticalSection(&cursor->lock);

	if (!GetConsoleScreenBufferInfo(cursor->console_handle, &info))
		return;

	maxx = info.srWindow.Right - info.srWindow.Left;
	maxy = info.srWindow.Bottom - info.srWindow.Top;
	if (pos.X < 0)
		pos.X = 0;
	else if (pos.X > maxx)
		pos.X = maxx;
	if (pos.Y < 0)
		pos.Y = 0;
	else if (pos.Y > maxy)
		pos.Y = maxy;
	SetConsoleCursorPosition(cursor->console_handle, pos);
}

static DWORD WINAPI
win32_stdio_input_thread(LPVOID data)
{
	struct win32_stdio_input_args	*args = data;
	int				 input_fd = args->input_fd;
	SOCKET				 input_bridge_socket = args->input_bridge_socket;
	HANDLE				 input_handle = INVALID_HANDLE_VALUE;
	char				 buffer[WIN32_STDIO_BUFFER];
	DWORD				 mode, read;
	int				 n, sent, offset;
	int				 empty_reads;

	free(args);
	args = NULL;

	if (input_fd != -1)
		input_handle = (HANDLE)_get_osfhandle(input_fd);
	empty_reads = 0;
	for (;;) {
		if (input_handle != INVALID_HANDLE_VALUE &&
		    GetConsoleMode(input_handle, &mode)) {
			if (!ReadFile(input_handle, buffer, sizeof buffer, &read,
			    NULL))
				break;
			if (read == 0) {
				if (++empty_reads > 16)
					break;
				Sleep(1);
				continue;
			}
			empty_reads = 0;
			n = (int)read;
		} else
			n = _read(input_fd, buffer, sizeof buffer);
		if (n <= 0)
			break;
		offset = 0;
		while (offset < n) {
			sent = send(input_bridge_socket, buffer + offset,
			    n - offset, 0);
			if (sent <= 0)
				goto out;
			offset += sent;
		}
	}

out:
	win32_socket_shutdown((uintptr_t)input_bridge_socket, 0);
	return (0);
}

static DWORD WINAPI
win32_stdio_output_thread(LPVOID data)
{
	struct win32_stdio_output_args	*args = data;
	int				 output_fd = args->output_fd;
	SOCKET				 output_bridge_socket = args->output_bridge_socket;
	int				 output_console = args->output_console;
	struct win32_stdio_cursor	*cursor = args->cursor;
	HANDLE				 output_handle = INVALID_HANDLE_VALUE;
	char				 buffer[WIN32_STDIO_BUFFER];
	DWORD				 written;
	int				 n, offset;

	free(args);
	args = NULL;

	if (output_fd != -1)
		output_handle = (HANDLE)_get_osfhandle(output_fd);
	for (;;) {
		n = recv(output_bridge_socket, buffer, sizeof buffer, 0);
		if (n <= 0)
			break;
		offset = 0;
		while (offset < n) {
			if (output_handle != INVALID_HANDLE_VALUE) {
				if (!WriteFile(output_handle, buffer + offset,
				    (DWORD)(n - offset), &written, NULL))
					goto out;
				if (written == 0)
					goto out;
				offset += (int)written;
			} else {
				written = (DWORD)_write(output_fd,
				    buffer + offset, n - offset);
				if ((int)written <= 0)
					goto out;
				offset += (int)written;
			}
		}
		if (output_console)
			win32_stdio_apply_console_cursor(cursor);
	}

out:
	win32_socket_shutdown((uintptr_t)output_bridge_socket, 0);
	return (0);
}

int
win32_stdio_bridge_open(struct win32_stdio_bridge *bridge, int input_fd,
    int output_fd, uintptr_t *input_socket, uintptr_t *output_socket,
    int input_console)
{
	uintptr_t	input_pair[2], output_pair[2];
	HANDLE		input_handle = INVALID_HANDLE_VALUE;
	DWORD		mode;
	struct win32_stdio_input_args	*input_args = NULL;
	struct win32_stdio_output_args	*output_args = NULL;

	if (bridge == NULL || input_socket == NULL || output_socket == NULL ||
	    input_fd == -1 || output_fd == -1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(bridge, 0, sizeof *bridge);

	/* Create shared cursor state. */
	bridge->cursor = calloc(1, sizeof *bridge->cursor);
	if (bridge->cursor == NULL)
		goto fail;
	InitializeCriticalSection(&bridge->cursor->lock);

	bridge->input_fd = -1;
	bridge->output_fd = -1;
	bridge->input_socket = (uintptr_t)INVALID_SOCKET;
	bridge->output_socket = (uintptr_t)INVALID_SOCKET;
	bridge->input_bridge_socket = (uintptr_t)INVALID_SOCKET;
	bridge->output_bridge_socket = (uintptr_t)INVALID_SOCKET;
	*input_socket = *output_socket = (uintptr_t)INVALID_SOCKET;
	bridge->input_fd = input_fd;
	bridge->output_fd = output_fd;
	input_handle = (HANDLE)_get_osfhandle(input_fd);
	if (input_console || (input_handle != INVALID_HANDLE_VALUE &&
	    GetConsoleMode(input_handle, &mode)))
		bridge->input_console = 1;
	if (win32_stdio_fd_handle(output_fd, &input_handle) == 0 &&
	    GetConsoleMode(input_handle, &mode)) {
		bridge->output_console = 1;
		bridge->cursor->console_handle = input_handle;
	}

	if (win32_socketpair(input_pair) != 0)
		goto fail;
	if (win32_socketpair(output_pair) != 0) {
		win32_socket_close(input_pair[0]);
		win32_socket_close(input_pair[1]);
		goto fail;
	}

	bridge->input_socket = input_pair[0];
	bridge->input_bridge_socket = input_pair[1];
	bridge->output_socket = output_pair[0];
	bridge->output_bridge_socket = output_pair[1];

	/* Create input worker with immutable args. */
	input_args = calloc(1, sizeof *input_args);
	if (input_args == NULL)
		goto fail;
	input_args->input_fd = bridge->input_fd;
	input_args->input_bridge_socket = (SOCKET)bridge->input_bridge_socket;

	if (!bridge->input_console) {
		bridge->input_thread = CreateThread(NULL, 0,
		    win32_stdio_input_thread, input_args, 0, NULL);
		if (bridge->input_thread == NULL)
			goto fail;
		/* Ownership transferred to thread. */
		input_args = NULL;
	} else {
		free(input_args);
		input_args = NULL;
	}

	/* Create output worker with immutable args. */
	output_args = calloc(1, sizeof *output_args);
	if (output_args == NULL)
		goto fail;
	output_args->output_fd = bridge->output_fd;
	output_args->output_bridge_socket = (SOCKET)bridge->output_bridge_socket;
	output_args->output_console = bridge->output_console;
	output_args->cursor = bridge->cursor;

	bridge->output_thread = CreateThread(NULL, 0,
	    win32_stdio_output_thread, output_args, 0, NULL);
	if (bridge->output_thread == NULL) {
		output_args->output_bridge_socket = INVALID_SOCKET;
		output_args->output_fd = -1;
		output_args->output_console = 0;
		output_args->cursor = NULL;
		free(output_args);
		output_args = NULL;
		goto fail;
	}
	/* Ownership transferred to thread. */
	output_args = NULL;

	*input_socket = bridge->input_socket;
	*output_socket = bridge->output_socket;
	return (0);

fail:
	free(input_args);
	free(output_args);
	win32_stdio_bridge_close(bridge);
	return (-1);
}

static int
win32_stdio_fd_handle(int fd, HANDLE *handle)
{
	intptr_t	value;

	if (fd == -1 || handle == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	value = _get_osfhandle(fd);
	if (value == -1) {
		SetLastError(ERROR_INVALID_HANDLE);
		return (-1);
	}
	*handle = (HANDLE)value;
	return (0);
}

int
win32_stdio_bridge_feed_input(struct win32_stdio_bridge *bridge,
    const void *data, size_t size)
{
	const char	*buf = data;
	SOCKET		 input_bridge_socket;
	int		 n, offset = 0;

	if (bridge == NULL || data == NULL ||
	    bridge->input_bridge_socket == (uintptr_t)INVALID_SOCKET) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	input_bridge_socket = (SOCKET)bridge->input_bridge_socket;
	while (offset < (int)size) {
		n = send(input_bridge_socket, buf + offset,
		    (int)size - offset, 0);
		if (n <= 0)
			return (-1);
		offset += n;
	}
	return (0);
}

int
win32_stdio_bridge_get_size(struct win32_stdio_bridge *bridge, unsigned int *sx,
    unsigned int *sy)
{
	CONSOLE_SCREEN_BUFFER_INFO	 info;
	HANDLE				 handle;

	if (bridge == NULL || sx == NULL || sy == NULL ||
	    bridge->output_fd == -1) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	if (win32_stdio_fd_handle(bridge->output_fd, &handle) != 0)
		return (-1);
	if (!GetConsoleScreenBufferInfo(handle, &info))
		return (-1);

	*sx = info.srWindow.Right - info.srWindow.Left + 1;
	*sy = info.srWindow.Bottom - info.srWindow.Top + 1;
	return (0);
}

int
win32_stdio_bridge_prepare_terminal(struct win32_stdio_bridge *bridge)
{
	DWORD	mode;
	HANDLE	handle;
	int	ok = 0;

	if (bridge == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	if (win32_stdio_fd_handle(bridge->input_fd, &handle) == 0 &&
	    GetConsoleMode(handle, &mode)) {
		if (!bridge->input_mode_valid) {
			bridge->input_mode = mode;
			bridge->input_mode_valid = 1;
		}
		if (!bridge->input_codepage_valid) {
			bridge->input_codepage = GetConsoleCP();
			if (bridge->input_codepage != 0)
				bridge->input_codepage_valid = 1;
		}
		if (bridge->input_codepage_valid)
			SetConsoleCP(CP_UTF8);
		mode &= ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|
		    ENABLE_PROCESSED_INPUT);
		mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
		if (SetConsoleMode(handle, mode))
			ok = 1;
		else if (SetConsoleMode(handle,
		    mode & ~ENABLE_VIRTUAL_TERMINAL_INPUT))
			ok = 1;
	}

	if (win32_stdio_fd_handle(bridge->output_fd, &handle) == 0 &&
	    GetConsoleMode(handle, &mode)) {
		if (!bridge->output_mode_valid) {
			bridge->output_mode = mode;
			bridge->output_mode_valid = 1;
		}
		if (!bridge->output_codepage_valid) {
			bridge->output_codepage = GetConsoleOutputCP();
			if (bridge->output_codepage != 0)
				bridge->output_codepage_valid = 1;
		}
		if (bridge->output_codepage_valid)
			SetConsoleOutputCP(CP_UTF8);
		mode |= ENABLE_PROCESSED_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|
		    DISABLE_NEWLINE_AUTO_RETURN;
		if (SetConsoleMode(handle, mode))
			ok = 1;
		else if (SetConsoleMode(handle, mode & ~DISABLE_NEWLINE_AUTO_RETURN))
			ok = 1;
	}

	return (ok ? 0 : -1);
}

void
win32_stdio_bridge_sync_console_cursor(struct win32_stdio_bridge *bridge,
    unsigned int x, unsigned int y)
{
	if (bridge == NULL || !bridge->output_console || bridge->cursor == NULL)
		return;

	EnterCriticalSection(&bridge->cursor->lock);
	bridge->cursor->pending_x = (SHORT)x;
	bridge->cursor->pending_y = (SHORT)y;
	bridge->cursor->pending_valid = 1;
	LeaveCriticalSection(&bridge->cursor->lock);

	win32_stdio_apply_console_cursor(bridge->cursor);
}

void
win32_stdio_bridge_restore_terminal(struct win32_stdio_bridge *bridge)
{
	HANDLE	handle;

	if (bridge == NULL)
		return;

	if (bridge->input_mode_valid &&
	    win32_stdio_fd_handle(bridge->input_fd, &handle) == 0) {
		SetConsoleMode(handle, bridge->input_mode);
		bridge->input_mode_valid = 0;
	}
	if (bridge->output_mode_valid &&
	    win32_stdio_fd_handle(bridge->output_fd, &handle) == 0) {
		SetConsoleMode(handle, bridge->output_mode);
		bridge->output_mode_valid = 0;
	}
	if (bridge->input_codepage_valid) {
		SetConsoleCP(bridge->input_codepage);
		bridge->input_codepage_valid = 0;
	}
	if (bridge->output_codepage_valid) {
		SetConsoleOutputCP(bridge->output_codepage);
		bridge->output_codepage_valid = 0;
	}
}

int
win32_stdio_bridge_close(struct win32_stdio_bridge *bridge)
{
	DWORD	result;
	int	forced = 0;

	if (bridge == NULL)
		return (0);

	win32_stdio_bridge_restore_terminal(bridge);
	win32_stdio_close_socket(&bridge->input_socket);
	win32_stdio_close_socket(&bridge->output_socket);
	win32_stdio_close_socket(&bridge->input_bridge_socket);
	win32_stdio_close_socket(&bridge->output_bridge_socket);

	if (bridge->input_thread != NULL)
		CancelSynchronousIo((HANDLE)bridge->input_thread);
	if (bridge->output_thread != NULL)
		CancelSynchronousIo((HANDLE)bridge->output_thread);

	/*
	 * Wait for worker threads to exit before freeing cursor state.
	 * Workers only access the cursor struct, not the bridge, so
	 * it's safe to free cursor after they exit.
	 */
	if (bridge->input_thread != NULL) {
		result = WaitForSingleObject((HANDLE)bridge->input_thread,
		    WIN32_STDIO_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			WaitForSingleObject((HANDLE)bridge->input_thread, 2000);
			forced = 1;
		}
		CloseHandle((HANDLE)bridge->input_thread);
		bridge->input_thread = NULL;
	}
	if (bridge->output_thread != NULL) {
		result = WaitForSingleObject((HANDLE)bridge->output_thread,
		    WIN32_STDIO_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			WaitForSingleObject((HANDLE)bridge->output_thread, 2000);
			forced = 1;
		}
		CloseHandle((HANDLE)bridge->output_thread);
		bridge->output_thread = NULL;
	}

	/* Now safe to free cursor state — all workers have exited. */
	if (bridge->cursor != NULL) {
		DeleteCriticalSection(&bridge->cursor->lock);
		free(bridge->cursor);
		bridge->cursor = NULL;
	}

	win32_stdio_close_fd(&bridge->input_fd);
	win32_stdio_close_fd(&bridge->output_fd);
	memset(bridge, 0, sizeof *bridge);
	bridge->input_fd = -1;
	bridge->output_fd = -1;
	bridge->input_socket = (uintptr_t)INVALID_SOCKET;
	bridge->output_socket = (uintptr_t)INVALID_SOCKET;
	bridge->input_bridge_socket = (uintptr_t)INVALID_SOCKET;
	bridge->output_bridge_socket = (uintptr_t)INVALID_SOCKET;

	return (forced);
}

#endif
