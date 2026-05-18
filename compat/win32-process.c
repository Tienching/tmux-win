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
#include <stdlib.h>
#include <string.h>

#include "win32-process.h"
#include "win32-socketpair.h"

#define WIN32_PROCESS_BUFFER 8192

static DWORD WINAPI
win32_process_close_handle_thread(LPVOID handle)
{
	CloseHandle((HANDLE)handle);
	return (0);
}

static void
win32_process_close_handle(void **handle)
{
	HANDLE	value, thread;

	if (*handle == NULL)
		return;
	value = (HANDLE)*handle;
	*handle = NULL;

	thread = CreateThread(NULL, 0, win32_process_close_handle_thread, value,
	    0, NULL);
	if (thread == NULL) {
		CloseHandle(value);
		return;
	}
	WaitForSingleObject(thread, 1000);
	CloseHandle(thread);
}

static void
win32_process_terminate_process_id(DWORD process_id, unsigned int exit_code)
{
	HANDLE	process;

	process = OpenProcess(PROCESS_TERMINATE, FALSE, process_id);
	if (process != NULL) {
		TerminateProcess(process, exit_code);
		CloseHandle(process);
	}
}

static void
win32_process_terminate_children(DWORD parent_id, unsigned int exit_code)
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
			win32_process_terminate_children(entry.th32ProcessID,
			    exit_code);
			win32_process_terminate_process_id(entry.th32ProcessID,
			    exit_code);
		}
	} while (Process32NextW(snapshot, &entry));

	CloseHandle(snapshot);
}

static DWORD WINAPI
win32_process_socket_to_stdin(LPVOID data)
{
	struct win32_process	*process = data;
	char			 buffer[WIN32_PROCESS_BUFFER];
	int			 n;
	DWORD			 written;

	for (;;) {
		n = recv((SOCKET)process->bridge_socket, buffer, sizeof buffer,
		    0);
		if (n <= 0)
			break;
		if (!WriteFile((HANDLE)process->input, buffer, n, &written,
		    NULL))
			break;
	}
	win32_process_close_handle(&process->input);
	return (0);
}

static DWORD WINAPI
win32_process_stdout_to_socket(LPVOID data)
{
	struct win32_process	*process = data;
	char			 buffer[WIN32_PROCESS_BUFFER];
	DWORD			 n;
	int			 sent, offset;

	for (;;) {
		if (!ReadFile((HANDLE)process->output, buffer, sizeof buffer,
		    &n, NULL) || n == 0)
			break;
		offset = 0;
		while (offset < (int)n) {
			sent = send((SOCKET)process->bridge_socket,
			    buffer + offset, (int)n - offset, 0);
			if (sent <= 0)
				goto out;
			offset += sent;
		}
	}

out:
	win32_process_close_handle(&process->output);
	win32_socket_shutdown(process->bridge_socket, 1);
	return (0);
}

static int
win32_process_make_job(HANDLE *job)
{
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION	limits;

	*job = CreateJobObjectW(NULL, NULL);
	if (*job == NULL)
		return (-1);
	memset(&limits, 0, sizeof limits);
	limits.BasicLimitInformation.LimitFlags =
	    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(*job, JobObjectExtendedLimitInformation,
	    &limits, sizeof limits)) {
		CloseHandle(*job);
		*job = NULL;
		return (-1);
	}
	return (0);
}

int
win32_process_spawn(struct win32_process *process,
    const struct win32_process_options *options, uintptr_t *master_socket)
{
	SECURITY_ATTRIBUTES	 sa;
	HANDLE			 stdin_read = NULL, stdin_write = NULL;
	HANDLE			 stdout_read = NULL, stdout_write = NULL;
	HANDLE			 stderr_write = NULL, job = NULL;
	STARTUPINFOW		 startup;
	PROCESS_INFORMATION	 pi;
	uintptr_t		 sockets[2];
	wchar_t			*command;
	DWORD			 flags = CREATE_SUSPENDED|CREATE_NO_WINDOW;

	if (process == NULL || master_socket == NULL || options == NULL ||
	    options->command == NULL || *options->command == L'\0') {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(process, 0, sizeof *process);
	memset(&pi, 0, sizeof pi);
	process->bridge_socket = (uintptr_t)INVALID_SOCKET;
	*master_socket = (uintptr_t)INVALID_SOCKET;

	if (win32_socketpair(sockets) != 0)
		return (-1);

	memset(&sa, 0, sizeof sa);
	sa.nLength = sizeof sa;
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0))
		goto fail;
	if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
		goto fail;
	if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0))
		goto fail;
	if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0))
		goto fail;

	if (options->show_stderr) {
		if (!DuplicateHandle(GetCurrentProcess(), stdout_write,
		    GetCurrentProcess(), &stderr_write, 0, TRUE,
		    DUPLICATE_SAME_ACCESS))
			goto fail;
	} else {
		stderr_write = CreateFileW(L"NUL", GENERIC_WRITE,
		    FILE_SHARE_READ|FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL, NULL);
		if (stderr_write == INVALID_HANDLE_VALUE) {
			stderr_write = NULL;
			goto fail;
		}
	}
	if (win32_process_make_job(&job) != 0)
		goto fail;

	command = _wcsdup(options->command);
	if (command == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto fail;
	}

	memset(&startup, 0, sizeof startup);
	startup.cb = sizeof startup;
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = stdin_read;
	startup.hStdOutput = stdout_write;
	startup.hStdError = stderr_write;
	if (options->environment != NULL)
		flags |= CREATE_UNICODE_ENVIRONMENT;

	if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, flags,
	    (LPVOID)options->environment, options->cwd, &startup, &pi)) {
		free(command);
		goto fail;
	}
	free(command);
	if (!AssignProcessToJobObject(job, pi.hProcess))
		goto fail;
	if (ResumeThread(pi.hThread) == (DWORD)-1)
		goto fail;

	CloseHandle(stdin_read);
	stdin_read = NULL;
	CloseHandle(stdout_write);
	stdout_write = NULL;
	CloseHandle(stderr_write);
	stderr_write = NULL;

	process->input = stdin_write;
	process->output = stdout_read;
	process->process = pi.hProcess;
	process->thread = pi.hThread;
	process->job = job;
	process->process_id = pi.dwProcessId;
	process->bridge_socket = sockets[1];
	stdin_write = NULL;
	stdout_read = NULL;
	pi.hProcess = NULL;
	pi.hThread = NULL;
	job = NULL;
	sockets[1] = (uintptr_t)INVALID_SOCKET;

	process->input_thread = CreateThread(NULL, 0,
	    win32_process_socket_to_stdin, process, 0, NULL);
	if (process->input_thread == NULL)
		goto fail;
	process->output_thread = CreateThread(NULL, 0,
	    win32_process_stdout_to_socket, process, 0, NULL);
	if (process->output_thread == NULL)
		goto fail;

	*master_socket = sockets[0];
	return (0);

fail:
	if (pi.hProcess != NULL)
		TerminateProcess(pi.hProcess, 1);
	if (pi.hThread != NULL)
		CloseHandle(pi.hThread);
	if (pi.hProcess != NULL)
		CloseHandle(pi.hProcess);
	if (stdin_read != NULL)
		CloseHandle(stdin_read);
	if (stdin_write != NULL)
		CloseHandle(stdin_write);
	if (stdout_read != NULL)
		CloseHandle(stdout_read);
	if (stdout_write != NULL)
		CloseHandle(stdout_write);
	if (stderr_write != NULL)
		CloseHandle(stderr_write);
	if (job != NULL)
		CloseHandle(job);
	if (sockets[0] != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(sockets[0]);
	if (sockets[1] != (uintptr_t)INVALID_SOCKET &&
	    sockets[1] != process->bridge_socket)
		win32_socket_close(sockets[1]);
	win32_process_close(process);
	return (-1);
}

int
win32_process_exited(const struct win32_process *process,
    unsigned long *exit_code)
{
	DWORD	code;

	if (process == NULL || process->process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (!GetExitCodeProcess((HANDLE)process->process, &code))
		return (-1);
	if (code == STILL_ACTIVE)
		return (0);
	if (exit_code != NULL)
		*exit_code = code;
	return (1);
}

int
win32_process_wait(struct win32_process *process, unsigned long timeout,
    unsigned long *exit_code)
{
	DWORD	result, code;

	if (process == NULL || process->process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	result = WaitForSingleObject((HANDLE)process->process, timeout);
	if (result == WAIT_TIMEOUT)
		return (0);
	if (result != WAIT_OBJECT_0)
		return (-1);
	if (!GetExitCodeProcess((HANDLE)process->process, &code))
		return (-1);
	if (exit_code != NULL)
		*exit_code = code;
	return (1);
}

int
win32_process_terminate(struct win32_process *process, unsigned int exit_code)
{
	if (process == NULL || process->process == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	win32_process_terminate_children(process->process_id, exit_code);
	if (process->job != NULL) {
		if (!TerminateJobObject((HANDLE)process->job, exit_code))
			return (-1);
		return (0);
	}
	if (!TerminateProcess((HANDLE)process->process, exit_code))
		return (-1);
	return (0);
}

void
win32_process_close(struct win32_process *process)
{
	uintptr_t	socket;

	if (process == NULL)
		return;

	socket = process->bridge_socket;
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_shutdown(socket, 0);
	win32_process_close_handle(&process->input);
	win32_process_close_handle(&process->output);
	win32_process_close_handle(&process->thread);
	win32_process_close_handle(&process->process);
	win32_process_close_handle(&process->job);
	if (process->input_thread != NULL) {
		WaitForSingleObject((HANDLE)process->input_thread, 1000);
		CloseHandle((HANDLE)process->input_thread);
	}
	if (process->output_thread != NULL) {
		WaitForSingleObject((HANDLE)process->output_thread, 1000);
		CloseHandle((HANDLE)process->output_thread);
	}
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_close(socket);
	memset(process, 0, sizeof *process);
	process->bridge_socket = (uintptr_t)INVALID_SOCKET;
}

unsigned long
win32_process_id(const struct win32_process *process)
{
	if (process == NULL)
		return (0);
	return (process->process_id);
}

#endif
