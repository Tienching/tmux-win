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
#include "win32-process-tree.h"
#include "win32-socketpair.h"

#define WIN32_PTY_BUFFER 8192
#define WIN32_PTY_CLOSE_TIMEOUT_MS 5000

/*
 * Independent thread argument structures.
 * Worker threads no longer hold a pointer to the parent struct win32_pty;
 * they only hold the immutable handle/socket values they need.  This
 * eliminates the use-after-close risk when win32_pty_close() frees
 * resources before workers have exited.
 */
struct win32_pty_input_args {
	SOCKET	 bridge_socket;
	HANDLE	 input;
	DWORD	 process_id;
};

struct win32_pty_output_args {
	SOCKET	 bridge_socket;
	HANDLE	 output;
};


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

/*
 * Global SRW lock to serialize Ctrl-Break console attach/detach
 * operations.  FreeConsole/AttachConsole/SetConsoleCtrlHandler are
 * process-wide state changes; concurrent calls from multiple threads
 * (e.g. multiple panes sending Ctrl-C simultaneously) would corrupt
 * the console attachment state.
 */
static SRWLOCK win32_ctrl_break_lock = SRWLOCK_INIT;

/*
 * Internal: send Ctrl-Break to a PID while holding the SRW lock.
 * Caller must NOT hold win32_ctrl_break_lock.
 */
static int
win32_pty_send_ctrl_break_to_pid_locked(DWORD pid)
{
	BOOL	attached, generated, had_console, handler_installed;

	if (pid == 0)
		return (-1);

	had_console = (GetConsoleWindow() != NULL);
	handler_installed = SetConsoleCtrlHandler(win32_pty_ignore_control,
	    TRUE);
	FreeConsole();
	attached = AttachConsole(pid);
	if (!attached) {
		/* Restore console state on failure. */
		if (had_console)
			AttachConsole(ATTACH_PARENT_PROCESS);
		if (handler_installed)
			SetConsoleCtrlHandler(win32_pty_ignore_control, FALSE);
		return (-1);
	}

	generated = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
	Sleep(50);
	FreeConsole();
	if (had_console)
		AttachConsole(ATTACH_PARENT_PROCESS);
	if (handler_installed)
		SetConsoleCtrlHandler(win32_pty_ignore_control, FALSE);
	return (generated ? 0 : -1);
}

/*
 * Send Ctrl-Break to a process identified only by PID.
 * Does not depend on struct win32_pty, so it can be called from
 * worker threads that only hold a process_id.
 * Serialized with a global SRW lock to prevent concurrent console
 * attach/detach from corrupting process-wide state.
 */
int
win32_pty_send_ctrl_break_to_pid(DWORD pid)
{
	int	rc;

	AcquireSRWLockExclusive(&win32_ctrl_break_lock);
	rc = win32_pty_send_ctrl_break_to_pid_locked(pid);
	ReleaseSRWLockExclusive(&win32_ctrl_break_lock);
	return (rc);
}

int
win32_pty_send_ctrl_break(struct win32_pty *pty)
{
	if (pty == NULL)
		return (-1);
	return (win32_pty_send_ctrl_break_to_pid(pty->conpty.process_id));
}

static int
win32_pty_write_input(HANDLE input, const char *buffer, int n)
{
	DWORD	written;

	while (n > 0) {
		if (!WriteFile(input, buffer, (DWORD)n, &written, NULL) ||
		    written == 0)
			return (-1);
		buffer += written;
		n -= (int)written;
	}
	return (0);
}

static DWORD WINAPI
win32_pty_socket_to_conpty(LPVOID data)
{
	struct win32_pty_input_args	*args = data;
	SOCKET				 bridge_socket = args->bridge_socket;
	HANDLE				 input = args->input;
	DWORD				 process_id = args->process_id;
	char				 buffer[WIN32_PTY_BUFFER];
	int				 i, n, offset;

	for (;;) {
		n = recv(bridge_socket, buffer, sizeof buffer, 0);
		if (n <= 0)
			break;
		offset = 0;
		for (i = 0; i < n; i++) {
			if (buffer[i] != '\003')
				continue;
			if (i != offset &&
			    win32_pty_write_input(input, buffer + offset,
			    i - offset) != 0)
				goto out;
			if (win32_pty_write_input(input, buffer + i, 1) != 0)
				goto out;
			win32_pty_send_ctrl_break_to_pid(process_id);
			offset = i + 1;
		}
		if (offset != n &&
		    win32_pty_write_input(input, buffer + offset,
		    n - offset) != 0)
			break;
	}

out:
	win32_socket_shutdown_read((uintptr_t)bridge_socket);
	return (0);
}

static DWORD WINAPI
win32_pty_conpty_to_socket(LPVOID data)
{
	struct win32_pty_output_args	*args = data;
	SOCKET				 bridge_socket = args->bridge_socket;
	HANDLE				 output = args->output;
	char				 buffer[WIN32_PTY_BUFFER];
	DWORD				 n;
	int				 sent, offset;

	for (;;) {
		if (!ReadFile(output, buffer, sizeof buffer, &n, NULL) ||
		    n == 0)
			break;
		offset = 0;
		while (offset < (int)n) {
			sent = send(bridge_socket, buffer + offset,
			    (int)n - offset, 0);
			if (sent <= 0)
				goto out;
			offset += sent;
		}
	}

out:
	win32_socket_shutdown((uintptr_t)bridge_socket, 1);
	return (0);
}

int
win32_pty_spawn(struct win32_pty *pty, const struct win32_pty_options *options,
    uintptr_t *master_socket)
{
	struct win32_pty_options	 defaults;
	struct win32_pty_input_args	*input_args = NULL;
	struct win32_pty_output_args	*output_args = NULL;
	uintptr_t			 sockets[2];

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

	input_args = calloc(1, sizeof *input_args);
	if (input_args == NULL)
		goto fail;
	input_args->bridge_socket = (SOCKET)pty->bridge_socket;
	input_args->input = (HANDLE)pty->conpty.input;
	input_args->process_id = pty->conpty.process_id;

	output_args = calloc(1, sizeof *output_args);
	if (output_args == NULL)
		goto fail;
	output_args->bridge_socket = (SOCKET)pty->bridge_socket;
	output_args->output = (HANDLE)pty->conpty.output;

	pty->input_thread = CreateThread(NULL, 0, win32_pty_socket_to_conpty,
	    input_args, 0, NULL);
	if (pty->input_thread == NULL)
		goto fail;
	/* Ownership of input_args transferred to input thread. */
	input_args = NULL;

	pty->output_thread = CreateThread(NULL, 0, win32_pty_conpty_to_socket,
	    output_args, 0, NULL);
	if (pty->output_thread == NULL) {
		/*
		 * Output thread failed to create.  The input thread is
		 * already running with its own args.  We must wait for it
		 * to exit before cleaning up, since the close path will
		 * shutdown the bridge socket which will cause the input
		 * thread to exit.
		 */
		output_args->bridge_socket = INVALID_SOCKET;
		output_args->output = NULL;
		free(output_args);
		output_args = NULL;
		goto fail;
	}
	/* Ownership of output_args transferred to output thread. */
	output_args = NULL;

	*master_socket = sockets[0];
	return (0);

fail:
	free(input_args);
	free(output_args);
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
	win32_process_tree_terminate_children(pty->conpty.process_id,
	    pty->conpty.process, exit_code);
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

	/* Step 1: shutdown bridge socket to unblock worker recv/send. */
	win32_shutdown_socket(socket);

	/* Step 2: cancel synchronous IO on worker threads. */
	if (pty->input_thread != NULL)
		CancelSynchronousIo((HANDLE)pty->input_thread);
	if (pty->output_thread != NULL)
		CancelSynchronousIo((HANDLE)pty->output_thread);

	/* Step 3: wait for child process. */
	if (pty->conpty.process != NULL) {
		result = WaitForSingleObject((HANDLE)pty->conpty.process, 1000);
		if (result == WAIT_TIMEOUT) {
			/*
			 * Child process did not exit in time.  Do not
			 * close ConPTY handles or memset the parent struct,
			 * since the child and workers may still be using
			 * them.  The caller must retry or use terminate.
			 */
			return;
		}
	}

	/* Step 4: wait for IO worker threads. */
	if (pty->input_thread != NULL) {
		result = WaitForSingleObject((HANDLE)pty->input_thread,
		    WIN32_PTY_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			/*
			 * Input worker did not exit.  Do not close ConPTY
			 * handles or memset — worker may still access them.
			 */
			return;
		}
		CloseHandle((HANDLE)pty->input_thread);
		pty->input_thread = NULL;
	}
	if (pty->output_thread != NULL) {
		result = WaitForSingleObject((HANDLE)pty->output_thread,
		    WIN32_PTY_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			/*
			 * Output worker did not exit.  Same reasoning:
			 * do not release resources it may still use.
			 */
			return;
		}
		CloseHandle((HANDLE)pty->output_thread);
		pty->output_thread = NULL;
	}

	/* Step 5: close ConPTY resources. */
	win32_conpty_close(&pty->conpty);

	/* Step 6: close bridge socket. */
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(socket);

	/* Step 7: zero the parent struct. */
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
