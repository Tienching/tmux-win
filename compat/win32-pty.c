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

/* Runs only in a disposable helper, never in the shared tmux server. */
int
win32_pty_ctrl_break_child(unsigned long pid)
{
	BOOL generated;
	DWORD members[1024], count, i;

	if (pid == 0)
		return (1);
	FreeConsole();
	if (!AttachConsole(pid)) return (1);
	/* Attach resets handlers: install protection only after attachment. */
	if (!SetConsoleCtrlHandler(win32_pty_ignore_control, TRUE)) return (1);
	count = GetConsoleProcessList(members, 1024);
	if (!count || count > 1024) return (1);
	for (i = 0; i < count && members[i] != pid; i++) {}
	if (i == count) return (1);
	/* Never fall back to group zero (console-wide broadcast). */
	generated = GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
	Sleep(50);
	FreeConsole();
	return (generated ? 0 : 1);
}

/* Hold the target process open until helper completion to prevent PID reuse. */
int
win32_pty_send_ctrl_break_to_pid(DWORD pid)
{
	wchar_t executable[32768], command[32832];
	STARTUPINFOW startup = {0};
	PROCESS_INFORMATION process = {0};
	HANDLE target;
	DWORD length, status = 1;
	if (!pid) return (-1);
	target = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (target == NULL) return (-1);
	if (WaitForSingleObject(target, 0) != WAIT_TIMEOUT) {
		CloseHandle(target);
		return (-1);
	}
	length = GetModuleFileNameW(NULL, executable, 32768);
	if (!length || length >= 32768) {
		CloseHandle(target);
		return (-1);
	}
	swprintf(command, 32832, L"\"%ls\" --win32-ctrl-break %lu", executable, pid);
	startup.cb = sizeof startup;
	if (CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
	    NULL, NULL, &startup, &process)) {
		if (WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0)
			GetExitCodeProcess(process.hProcess, &status);
		else
			TerminateProcess(process.hProcess, 1);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
	CloseHandle(target);
	return (status == 0 ? 0 : -1);
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
	char				 buffer[WIN32_PTY_BUFFER];
	int				 n;

	free(args);
	args = NULL;

	for (;;) {
		n = recv(bridge_socket, buffer, sizeof buffer, 0);
		if (n <= 0)
			break;
		/* ConPTY owns ETX interpretation; never add a Ctrl-Break to it. */
		if (win32_pty_write_input(input, buffer, n) != 0)
			break;
	}

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

	free(args);
	args = NULL;

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
	DWORD				 saved_error;

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
	saved_error = GetLastError();
	free(input_args);
	free(output_args);
	if (sockets[0] != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(sockets[0]);
	if (sockets[1] != (uintptr_t)INVALID_SOCKET &&
	    sockets[1] != pty->bridge_socket)
		win32_socket_close(sockets[1]);
	win32_pty_close(pty);
	SetLastError(saved_error);
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
	/*
	 * Always call TerminateProcess regardless of whether job is set.
	 * When job is non-NULL, TerminateJobObject kills all processes in
	 * the job, but TerminateProcess is still needed as a safety net
	 * in case the process was not fully assigned.  When job is NULL
	 * (parent-job fallback), only TerminateProcess applies.
	 */
	if (pty->conpty.job != NULL)
		TerminateJobObject((HANDLE)pty->conpty.job, exit_code);
	if (!TerminateProcess((HANDLE)pty->conpty.process, exit_code))
		return (-1);
	return (0);
}

int
win32_pty_close(struct win32_pty *pty)
{
	uintptr_t	socket;
	DWORD		result;
	int		forced = 0;

	if (pty == NULL)
		return (0);

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
			 * Child process did not exit in time.  Force
			 * terminate so we can safely close resources.
			 */
			win32_pty_terminate(pty, 1);
			forced = 1;
			WaitForSingleObject((HANDLE)pty->conpty.process, 2000);
		}
	}

	/* Step 4: wait for IO worker threads. */
	if (pty->input_thread != NULL) {
		result = WaitForSingleObject((HANDLE)pty->input_thread,
		    WIN32_PTY_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			/*
			 * Input worker still running.  The bridge socket
			 * is shut down and IO is cancelled, so the worker
			 * should exit soon.  Wait a bit longer.
			 */
			WaitForSingleObject((HANDLE)pty->input_thread, 2000);
			forced = 1;
		}
		CloseHandle((HANDLE)pty->input_thread);
		pty->input_thread = NULL;
	}
	if (pty->output_thread != NULL) {
		result = WaitForSingleObject((HANDLE)pty->output_thread,
		    WIN32_PTY_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			WaitForSingleObject((HANDLE)pty->output_thread, 2000);
			forced = 1;
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

	return (forced);
}

unsigned long
win32_pty_process_id(const struct win32_pty *pty)
{
	if (pty == NULL)
		return (0);
	return (pty->conpty.process_id);
}

#endif
