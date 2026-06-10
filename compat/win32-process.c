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
#include <stdlib.h>
#include <string.h>

#include "win32-process.h"
#include "win32-process-tree.h"
#include "win32-job.h"
#include "win32-socketpair.h"

#define WIN32_PROCESS_BUFFER 8192
#define WIN32_PROCESS_CLOSE_TIMEOUT_MS 5000

/*
 * Independent thread argument structures.
 * Worker threads no longer hold a pointer to the parent struct win32_process;
 * they only hold the immutable handle/socket values they need.
 */
struct win32_process_input_args {
	SOCKET	 bridge_socket;
	HANDLE	 input;
};

struct win32_process_output_args {
	SOCKET	 bridge_socket;
	HANDLE	 output;
};

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


static DWORD WINAPI
win32_process_socket_to_stdin(LPVOID data)
{
	struct win32_process_input_args	*args = data;
	SOCKET				 bridge_socket = args->bridge_socket;
	HANDLE				 input = args->input;
	char				 buffer[WIN32_PROCESS_BUFFER];
	int				 n;
	DWORD				 written;

	for (;;) {
		n = recv(bridge_socket, buffer, sizeof buffer, 0);
		if (n <= 0)
			break;
		if (!WriteFile(input, buffer, (DWORD)n, &written, NULL))
			break;
	}
	win32_process_close_handle((void **)&input);
	if (bridge_socket != INVALID_SOCKET)
		win32_socket_shutdown_read((uintptr_t)bridge_socket);
	return (0);
}

static DWORD WINAPI
win32_process_stdout_to_socket(LPVOID data)
{
	struct win32_process_output_args	*args = data;
	SOCKET				 bridge_socket = args->bridge_socket;
	HANDLE				 output = args->output;
	char				 buffer[WIN32_PROCESS_BUFFER];
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
	win32_process_close_handle((void **)&output);
	win32_socket_shutdown((uintptr_t)bridge_socket, 1);
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
	STARTUPINFOEXW		 startup;
	PROCESS_INFORMATION	 pi;
	SIZE_T			 attributes_size = 0;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	HANDLE			 inherit_handles[3];
	uintptr_t		 sockets[2];
	wchar_t			*command;
	DWORD			 flags = EXTENDED_STARTUPINFO_PRESENT |
	    CREATE_SUSPENDED|CREATE_NO_WINDOW|
	    win32_job_creation_flags_for_child();
	int			 attributes_initialized = 0;
	struct win32_process_input_args	*input_args = NULL;
	struct win32_process_output_args *output_args = NULL;

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
	if (options->discard_stdout) {
		stdout_write = CreateFileW(L"NUL", GENERIC_WRITE,
		    FILE_SHARE_READ|FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL, NULL);
		if (stdout_write == INVALID_HANDLE_VALUE) {
			stdout_write = NULL;
			goto fail;
		}
	} else {
		if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
			goto fail;
		if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0))
			goto fail;
	}
	if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0))
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

	inherit_handles[0] = stdin_read;
	inherit_handles[1] = stdout_write;
	inherit_handles[2] = stderr_write;
	InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
	attributes = HeapAlloc(GetProcessHeap(), 0, attributes_size);
	if (attributes == NULL) {
		SetLastError(ERROR_OUTOFMEMORY);
		goto fail;
	}
	if (!InitializeProcThreadAttributeList(attributes, 1, 0,
	    &attributes_size))
		goto fail;
	attributes_initialized = 1;
	if (!UpdateProcThreadAttribute(attributes, 0,
	    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_handles,
	    sizeof inherit_handles, NULL, NULL))
		goto fail;

	command = _wcsdup(options->command);
	if (command == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto fail;
	}

	memset(&startup, 0, sizeof startup);
	startup.StartupInfo.cb = sizeof startup;
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = stdin_read;
	startup.StartupInfo.hStdOutput = stdout_write;
	startup.StartupInfo.hStdError = stderr_write;
	startup.lpAttributeList = attributes;
	if (options->environment != NULL)
		flags |= CREATE_UNICODE_ENVIRONMENT;

	if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, flags,
	    (LPVOID)options->environment, options->cwd, &startup.StartupInfo,
	    &pi)) {
		free(command);
		goto fail;
	}
	free(command);
	if (win32_job_assign_or_fallback(job, pi.hProcess) != 0)
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

	input_args = calloc(1, sizeof *input_args);
	if (input_args == NULL)
		goto fail;
	input_args->bridge_socket = (SOCKET)process->bridge_socket;
	input_args->input = (HANDLE)process->input;

	process->input_thread = CreateThread(NULL, 0,
	    win32_process_socket_to_stdin, input_args, 0, NULL);
	if (process->input_thread == NULL)
		goto fail;
	/* Ownership of input_args transferred to input thread. */
	input_args = NULL;

	if (process->output != NULL) {
		output_args = calloc(1, sizeof *output_args);
		if (output_args == NULL) {
			/*
			 * Input thread already running. Shutdown bridge
			 * socket to let it exit, then fall through to
			 * win32_process_close which waits for the thread.
			 */
			goto fail;
		}
		output_args->bridge_socket = (SOCKET)process->bridge_socket;
		output_args->output = (HANDLE)process->output;

		process->output_thread = CreateThread(NULL, 0,
		    win32_process_stdout_to_socket, output_args, 0, NULL);
		if (process->output_thread == NULL) {
			output_args->bridge_socket = INVALID_SOCKET;
			output_args->output = NULL;
			free(output_args);
			output_args = NULL;
			goto fail;
		}
		/* Ownership of output_args transferred to output thread. */
		output_args = NULL;
	}

	DeleteProcThreadAttributeList(attributes);
	HeapFree(GetProcessHeap(), 0, attributes);

	*master_socket = sockets[0];
	return (0);

fail:
	free(input_args);
	free(output_args);
	if (pi.hProcess != NULL)
		TerminateProcess(pi.hProcess, 1);
	if (pi.hThread != NULL)
		CloseHandle(pi.hThread);
	if (pi.hProcess != NULL)
		CloseHandle(pi.hProcess);
	if (attributes != NULL) {
		if (attributes_initialized)
			DeleteProcThreadAttributeList(attributes);
		HeapFree(GetProcessHeap(), 0, attributes);
	}
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
	win32_process_tree_terminate_children(process->process_id,
	    process->process, exit_code);
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
	DWORD		result;

	if (process == NULL)
		return;

	socket = process->bridge_socket;
	if (socket != (uintptr_t)INVALID_SOCKET)
		win32_socket_shutdown(socket, 0);

	if (process->input_thread != NULL)
		CancelSynchronousIo((HANDLE)process->input_thread);
	if (process->output_thread != NULL)
		CancelSynchronousIo((HANDLE)process->output_thread);

	/* Wait for IO worker threads. */
	if (process->input_thread != NULL) {
		result = WaitForSingleObject((HANDLE)process->input_thread,
		    WIN32_PROCESS_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			/*
			 * Input worker did not exit.  Do not close handles
			 * or memset — worker may still access them.
			 */
			return;
		}
		CloseHandle((HANDLE)process->input_thread);
		process->input_thread = NULL;
	}
	if (process->output_thread != NULL) {
		result = WaitForSingleObject((HANDLE)process->output_thread,
		    WIN32_PROCESS_CLOSE_TIMEOUT_MS);
		if (result != WAIT_OBJECT_0) {
			/*
			 * Output worker did not exit.  Same reasoning:
			 * do not release resources it may still use.
			 */
			return;
		}
		CloseHandle((HANDLE)process->output_thread);
		process->output_thread = NULL;
	}

	win32_process_close_handle(&process->process);
	win32_process_close_handle(&process->job);
	win32_process_close_handle(&process->input);
	win32_process_close_handle(&process->output);
	win32_process_close_handle(&process->thread);
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

int
win32_native_exit_to_status(unsigned long native)
{
	int	signo;

	/*
	 * Most Windows processes exit with a small (0..255) value through
	 * ExitProcess(); leave room for tools that explicitly use values up
	 * to 0xFF and synthesize a POSIX-shaped (code << 8) status, the same
	 * convention macOS/Linux waitpid() uses.
	 */
	if (native <= 0xff)
		return ((int)((native & 0xff) << 8));

	/*
	 * NTSTATUS-shaped failures (high bit or DBG_* range) get mapped to
	 * the closest POSIX signal so log lines / ${pane_dead_signal} stay
	 * meaningful. Everything not recognised collapses to SIGTERM (15)
	 * rather than being silently mis-decoded as an exit code.
	 */
	switch (native) {
	case 0xC0000005UL: /* STATUS_ACCESS_VIOLATION */
	case 0xC0000094UL: /* STATUS_INTEGER_DIVIDE_BY_ZERO */
	case 0xC0000096UL: /* STATUS_PRIVILEGED_INSTRUCTION */
		signo = 11; /* SIGSEGV */
		break;
	case 0xC000001DUL: /* STATUS_ILLEGAL_INSTRUCTION */
		signo = 4;  /* SIGILL */
		break;
	case 0xC000013AUL: /* STATUS_CONTROL_C_EXIT */
	case 0x40010005UL: /* DBG_CONTROL_C */
		signo = 2;  /* SIGINT */
		break;
	case 0xC0000142UL: /* STATUS_DLL_INIT_FAILED */
	case 0xC0000409UL: /* STATUS_STACK_BUFFER_OVERRUN / __fastfail */
		signo = 6;  /* SIGABRT */
		break;
	case 0x40010004UL: /* DBG_TERMINATE_THREAD */
	case 0x40010003UL: /* DBG_TERMINATE_PROCESS */
	case 0xC000013BUL: /* STATUS_LOCAL_DISCONNECT */
		signo = 15; /* SIGTERM */
		break;
	default:
		signo = 15;
		break;
	}

	/*
	 * Encode as (signo & 0x7f) so WIFSIGNALED((status)) is true and
	 * WTERMSIG((status)) returns signo. WIFEXITED is intentionally
	 * false for these cases.
	 */
	return (signo & 0x7f);
}

#endif
