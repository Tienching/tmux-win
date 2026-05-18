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
#include <tlhelp32.h>
#include <windows.h>

#include <stdint.h>
#include <string.h>

#include "win32-pty.h"
#include "win32-socketpair.h"

#define WIN32_PTY_BUFFER 8192

static void
win32_pty_terminate_process_id(DWORD process_id, unsigned int exit_code)
{
	HANDLE	process;

	process = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
	if (process != NULL) {
		TerminateProcess(process, exit_code);
		CloseHandle(process);
	}
}

static void
win32_pty_terminate_children(DWORD parent_id, unsigned int exit_code)
{
	HANDLE		 snapshot;
	PROCESSENTRY32W	 entry;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	memset(&entry, 0, sizeof entry);
	entry.dwSize = sizeof entry;
	if (!Process32FirstW(snapshot, &entry)) {
		CloseHandle(snapshot);
		return;
	}

	do {
		if (entry.th32ParentProcessID == parent_id) {
			win32_pty_terminate_children(entry.th32ProcessID,
			    exit_code);
			win32_pty_terminate_process_id(entry.th32ProcessID,
			    exit_code);
		}
	} while (Process32NextW(snapshot, &entry));

	CloseHandle(snapshot);
}

static void
win32_shutdown_socket(uintptr_t socket)
{
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_shutdown(socket, 0);
}

static BOOL WINAPI
win32_pty_ignore_control(DWORD type)
{
	(void)type;
	return (TRUE);
}

int
win32_pty_send_ctrl_break(struct win32_pty *pty)
{
	DWORD	pid;
	BOOL	attached, generated, had_console;

	pid = pty->conpty.process_id;
	if (pid == 0)
		return (-1);

	had_console = (GetConsoleWindow() != NULL);
	SetConsoleCtrlHandler(win32_pty_ignore_control, TRUE);
	FreeConsole();
	attached = AttachConsole(pid);
	if (!attached) {
		if (had_console)
			AttachConsole(ATTACH_PARENT_PROCESS);
		SetConsoleCtrlHandler(win32_pty_ignore_control, FALSE);
		return (-1);
	}

	generated = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
	Sleep(50);
	FreeConsole();
	if (had_console)
		AttachConsole(ATTACH_PARENT_PROCESS);
	SetConsoleCtrlHandler(win32_pty_ignore_control, FALSE);
	return (generated ? 0 : -1);
}

static int
win32_pty_write_input(struct win32_pty *pty, const char *buffer, int n)
{
	DWORD	written;

	while (n > 0) {
		if (!WriteFile((HANDLE)pty->conpty.input, buffer, (DWORD)n,
		    &written, NULL) || written == 0)
			return (-1);
		buffer += written;
		n -= (int)written;
	}
	return (0);
}

static DWORD WINAPI
win32_pty_socket_to_conpty(LPVOID data)
{
	struct win32_pty	*pty = data;
	char			 buffer[WIN32_PTY_BUFFER];
	int			 i, n, offset;

	for (;;) {
		n = recv((SOCKET)pty->bridge_socket, buffer, sizeof buffer, 0);
		if (n <= 0)
			break;
		offset = 0;
		for (i = 0; i < n; i++) {
			if (buffer[i] != '\003')
				continue;
			if (i != offset &&
			    win32_pty_write_input(pty, buffer + offset,
			    i - offset) != 0)
				goto out;
			if (win32_pty_write_input(pty, buffer + i, 1) != 0)
				goto out;
			win32_pty_send_ctrl_break(pty);
			offset = i + 1;
		}
		if (offset != n &&
		    win32_pty_write_input(pty, buffer + offset, n - offset) != 0)
			break;
	}

out:
	win32_socket_shutdown(pty->bridge_socket, 0);
	return (0);
}

static DWORD WINAPI
win32_pty_conpty_to_socket(LPVOID data)
{
	struct win32_pty	*pty = data;
	char			 buffer[WIN32_PTY_BUFFER];
	DWORD			 n;
	int			 sent, offset;

	for (;;) {
		if (!ReadFile((HANDLE)pty->conpty.output, buffer, sizeof buffer,
		    &n, NULL) || n == 0)
			break;
		offset = 0;
		while (offset < (int)n) {
			sent = send((SOCKET)pty->bridge_socket, buffer + offset,
			    (int)n - offset, 0);
			if (sent <= 0)
				goto out;
			offset += sent;
		}
	}

out:
	win32_socket_shutdown(pty->bridge_socket, 0);
	return (0);
}

int
win32_pty_spawn(struct win32_pty *pty, const struct win32_pty_options *options,
    uintptr_t *master_socket)
{
	struct win32_pty_options	 defaults;
	uintptr_t		 sockets[2];

	if (pty == NULL || master_socket == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(pty, 0, sizeof *pty);
	pty->bridge_socket = (uintptr_t)INVALID_SOCKET;
	*master_socket = (uintptr_t)INVALID_SOCKET;

	if (options == NULL) {
		memset(&defaults, 0, sizeof defaults);
		options = &defaults;
	}

	if (win32_socketpair(sockets) != 0)
		return (-1);
	if (win32_conpty_spawn(&pty->conpty, options->command, options->cwd,
	    options->environment, options->columns, options->rows) != 0)
		goto fail;

	pty->bridge_socket = sockets[1];
	pty->input_thread = CreateThread(NULL, 0, win32_pty_socket_to_conpty,
	    pty, 0, NULL);
	if (pty->input_thread == NULL)
		goto fail;
	pty->output_thread = CreateThread(NULL, 0, win32_pty_conpty_to_socket,
	    pty, 0, NULL);
	if (pty->output_thread == NULL)
		goto fail;

	*master_socket = sockets[0];
	return (0);

fail:
	if (sockets[0] != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(sockets[0]);
	if (sockets[1] != (uintptr_t)INVALID_SOCKET &&
	    sockets[1] != pty->bridge_socket)
		win32_socket_close(sockets[1]);
	win32_pty_close(pty);
	return (-1);
}

int
win32_pty_resize(struct win32_pty *pty, unsigned short columns,
    unsigned short rows)
{
	if (pty == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	return (win32_conpty_resize(&pty->conpty, columns, rows));
}

int
win32_pty_exited(const struct win32_pty *pty, unsigned long *exit_code)
{
	DWORD	code;

	if (pty == NULL || pty->conpty.process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (!GetExitCodeProcess((HANDLE)pty->conpty.process, &code))
		return (-1);
	if (code == STILL_ACTIVE)
		return (0);
	if (exit_code != NULL)
		*exit_code = code;
	return (1);
}

int
win32_pty_wait(struct win32_pty *pty, unsigned long timeout,
    unsigned long *exit_code)
{
	DWORD	result, code;

	if (pty == NULL || pty->conpty.process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	result = WaitForSingleObject((HANDLE)pty->conpty.process, timeout);
	if (result == WAIT_TIMEOUT)
		return (0);
	if (result != WAIT_OBJECT_0)
		return (-1);
	if (!GetExitCodeProcess((HANDLE)pty->conpty.process, &code))
		return (-1);
	if (exit_code != NULL)
		*exit_code = code;
	return (1);
}

int
win32_pty_terminate(struct win32_pty *pty, unsigned int exit_code)
{
	if (pty == NULL || pty->conpty.process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	win32_pty_terminate_children(pty->conpty.process_id, exit_code);
	if (pty->conpty.job != NULL) {
		if (!TerminateJobObject((HANDLE)pty->conpty.job, exit_code))
			return (-1);
		return (0);
	}
	if (!TerminateProcess((HANDLE)pty->conpty.process, exit_code))
		return (-1);
	return (0);
}

void
win32_pty_close(struct win32_pty *pty)
{
	uintptr_t	socket;
	DWORD		result;

	if (pty == NULL)
		return;

	socket = pty->bridge_socket;
	win32_shutdown_socket(socket);
	if (pty->conpty.process != NULL) {
		result = WaitForSingleObject((HANDLE)pty->conpty.process, 1000);
		if (result == WAIT_TIMEOUT)
			pty->conpty.pseudoconsole = NULL;
	}
	win32_conpty_close(&pty->conpty);
	if (pty->input_thread != NULL) {
		WaitForSingleObject((HANDLE)pty->input_thread, 1000);
		CloseHandle((HANDLE)pty->input_thread);
	}
	if (pty->output_thread != NULL) {
		WaitForSingleObject((HANDLE)pty->output_thread, 1000);
		CloseHandle((HANDLE)pty->output_thread);
	}
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(socket);
	memset(pty, 0, sizeof *pty);
	pty->bridge_socket = (uintptr_t)INVALID_SOCKET;
}

unsigned long
win32_pty_process_id(const struct win32_pty *pty)
{
	if (pty == NULL)
		return (0);
	return (pty->conpty.process_id);
}

#endif
