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
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#include "win32-conpty.h"
#include "win32-job.h"
#include "win32-process-tree.h"

#ifndef __unused
#ifdef __GNUC__
#define __unused __attribute__((unused))
#else
#define __unused
#endif
#endif

typedef HRESULT (WINAPI *win32_create_pseudoconsole_fn)(COORD, HANDLE, HANDLE,
    DWORD, HPCON *);
typedef HRESULT (WINAPI *win32_resize_pseudoconsole_fn)(HPCON, COORD);
typedef VOID (WINAPI *win32_close_pseudoconsole_fn)(HPCON);

struct win32_conpty_api {
	win32_create_pseudoconsole_fn	create;
	win32_resize_pseudoconsole_fn	resize;
	win32_close_pseudoconsole_fn	close;
};

static INIT_ONCE		 conpty_once = INIT_ONCE_STATIC_INIT;
static struct win32_conpty_api conpty_api;

static BOOL CALLBACK
win32_conpty_init_once(__unused PINIT_ONCE once, __unused PVOID parameter,
    __unused PVOID *context)
{
	HMODULE	kernel32;
	union {
		FARPROC				proc;
		win32_create_pseudoconsole_fn	create;
	} create;
	union {
		FARPROC				proc;
		win32_resize_pseudoconsole_fn	resize;
	} resize;
	union {
		FARPROC				proc;
		win32_close_pseudoconsole_fn	close;
	} close;

	kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (kernel32 == NULL)
		return (FALSE);

	create.proc = GetProcAddress(kernel32, "CreatePseudoConsole");
	resize.proc = GetProcAddress(kernel32, "ResizePseudoConsole");
	close.proc = GetProcAddress(kernel32, "ClosePseudoConsole");
	conpty_api.create = create.create;
	conpty_api.resize = resize.resize;
	conpty_api.close = close.close;
	if (conpty_api.create == NULL || conpty_api.resize == NULL ||
	    conpty_api.close == NULL)
		return (FALSE);
	return (TRUE);
}

static int
win32_conpty_init(void)
{
	if (!InitOnceExecuteOnce(&conpty_once, win32_conpty_init_once, NULL,
	    NULL))
		return (-1);
	return (0);
}

static wchar_t *
win32_wcsdup(const wchar_t *s)
{
	wchar_t	*copy;
	size_t	 len;

	len = wcslen(s) + 1;
	copy = malloc(len * sizeof *copy);
	if (copy == NULL)
		return (NULL);
	memcpy(copy, s, len * sizeof *copy);
	return (copy);
}

static DWORD WINAPI
win32_close_handle_thread(LPVOID handle)
{
	CloseHandle((HANDLE)handle);
	return (0);
}

static void
win32_close_handle(void **handle)
{
	HANDLE	value, thread;

	if (*handle == NULL)
		return;
	value = (HANDLE)*handle;
	*handle = NULL;

	thread = CreateThread(NULL, 0, win32_close_handle_thread, value, 0,
	    NULL);
	if (thread == NULL) {
		CloseHandle(value);
		return;
	}
	WaitForSingleObject(thread, 1000);
	CloseHandle(thread);
}

struct win32_close_pseudoconsole_data {
	HPCON	pseudoconsole;
};

static DWORD WINAPI
win32_close_pseudoconsole_thread(LPVOID data0)
{
	struct win32_close_pseudoconsole_data	*data = data0;

	conpty_api.close(data->pseudoconsole);
	free(data);
	return (0);
}

static void
win32_close_pseudoconsole(HPCON pseudoconsole)
{
	struct win32_close_pseudoconsole_data	*data;
	HANDLE					 thread;

	if (pseudoconsole == NULL || win32_conpty_init() != 0)
		return;

	data = malloc(sizeof *data);
	if (data == NULL)
		return;
	data->pseudoconsole = pseudoconsole;
	thread = CreateThread(NULL, 0, win32_close_pseudoconsole_thread,
	    data, 0, NULL);
	if (thread == NULL) {
		free(data);
		return;
	}
	if (WaitForSingleObject(thread, 1000) == WAIT_TIMEOUT)
		;
	CloseHandle(thread);
}

/* The wrapper stays alive; the actual command handles console events. */
static BOOL WINAPI
win32_conpty_child_control(DWORD event)
{
	return (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT);
}

int
win32_conpty_child_main(void)
{
	wchar_t *line = GetCommandLineW(), *command;
	STARTUPINFOEXW startup;
	PROCESS_INFORMATION process;
	DWORD status = 1, error = ERROR_INVALID_PARAMETER, written;
	SECURITY_ATTRIBUTES security = { sizeof security, NULL, TRUE };
	HANDLE input = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE, ack;
	HANDLE inherited[2];
	SIZE_T attributes_size = 0;
	int attributes_initialized = 0;

	/* Skip our executable and the internal switch, preserving raw quoting. */
	if (*line == L'"') {
		line++;
		while (*line && *line != L'"') line++;
		if (*line) line++;
	} else {
		while (*line && *line != L' ' && *line != L'\t') line++;
	}
	while (*line == L' ' || *line == L'\t') line++;
	while (*line && *line != L' ' && *line != L'\t') line++;
	while (*line == L' ' || *line == L'\t') line++;
	ack = (HANDLE)(uintptr_t)wcstoull(line, &line, 10);
	if (ack == NULL || ack == INVALID_HANDLE_VALUE) return (1);
	/* The startup channel must never leak into the actual command. */
	if (!SetHandleInformation(ack, HANDLE_FLAG_INHERIT, 0)) return (1);
	while (*line == L' ' || *line == L'\t') line++;
	if (!*line) return (1);
	command = win32_wcsdup(line);
	if (command == NULL) return (1);
	memset(&startup, 0, sizeof startup);
	memset(&process, 0, sizeof process);
	/* CREATE_NEW_PROCESS_GROUP disables Ctrl-C. Reset only in this child. */
	if (!SetConsoleCtrlHandler(NULL, FALSE) ||
	    !SetConsoleCtrlHandler(win32_conpty_child_control, TRUE)) {
		error = GetLastError();
		goto child_done;
	}
	startup.StartupInfo.cb = sizeof startup;
	/* GUI-subsystem tmux may not have initialized standard handles. */
	input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
	output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, NULL);
	if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
		error = GetLastError();
		goto child_done;
	}
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = input;
	startup.StartupInfo.hStdOutput = output;
	startup.StartupInfo.hStdError = output;
	InitializeProcThreadAttributeList(NULL, 1, 0, &attributes_size);
	startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attributes_size);
	if (startup.lpAttributeList == NULL) {
		error = ERROR_OUTOFMEMORY;
		goto child_done;
	}
	if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
	    &attributes_size)) {
		error = GetLastError();
		goto child_done;
	}
	attributes_initialized = 1;
	inherited[0] = input;
	inherited[1] = output;
	if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
	    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited,
	    NULL, NULL) || !CreateProcessW(NULL, command, NULL, NULL, TRUE,
	    EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &startup.StartupInfo,
	    &process)) {
		error = GetLastError();
		goto child_done;
	}
	error = ERROR_SUCCESS;
child_done:
	if ((!WriteFile(ack, &error, sizeof error, &written, NULL) ||
	    written != sizeof error) && process.hProcess != NULL) {
		win32_process_tree_terminate_children(process.dwProcessId,
		    process.hProcess, 1);
		TerminateProcess(process.hProcess, 1);
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
		error = ERROR_BROKEN_PIPE;
	}
	CloseHandle(ack);
	if (attributes_initialized)
		DeleteProcThreadAttributeList(startup.lpAttributeList);
	if (startup.lpAttributeList != NULL)
		HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
	if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
	if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
	free(command);
	if (error != ERROR_SUCCESS) return (1);
	CloseHandle(process.hThread);
	if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
		GetExitCodeProcess(process.hProcess, &status);
	CloseHandle(process.hProcess);
	return ((int)status);
}

static wchar_t *
win32_conpty_child_command(const wchar_t *command, HANDLE ack)
{
	wchar_t executable[32768], *result;
	DWORD length;
	size_t capacity;

	length = GetModuleFileNameW(NULL, executable, 32768);
	if (!length || length >= 32768) return (NULL);
	capacity = length + wcslen(command) + 64;
	if (capacity > 32767) {
		SetLastError(ERROR_FILENAME_EXCED_RANGE);
		return (NULL);
	}
	result = calloc(capacity, sizeof *result);
	if (result == NULL) return (NULL);
	swprintf(result, capacity, L"\"%ls\" --win32-conpty-child %llu %ls",
	    executable, (unsigned long long)(uintptr_t)ack, command);
	return (result);
}

int
win32_conpty_available(void)
{
	return (win32_conpty_init() == 0);
}

int
win32_conpty_spawn(struct win32_conpty *pty, const wchar_t *command,
    const wchar_t *cwd, const wchar_t *environment, unsigned short columns,
    unsigned short rows)
{
	COORD			 size;
	HANDLE			 input_read = NULL, input_write = NULL;
	HANDLE			 output_read = NULL, output_write = NULL;
	HANDLE			 job = NULL;
	HANDLE			 ack_read = NULL, ack_write = NULL;
	SECURITY_ATTRIBUTES	 security = { sizeof security, NULL, TRUE };
	HPCON			 pseudoconsole = NULL;
	STARTUPINFOEXW		 startup;
	PROCESS_INFORMATION	 process;
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
	SIZE_T			 attributes_size = 0;
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = NULL;
	wchar_t			*mutable_command = NULL;
	HRESULT			 hr;
	int			 attributes_initialized = 0;
	DWORD			 saved_error;
	DWORD			 creation_flags = EXTENDED_STARTUPINFO_PRESENT |
	    CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP |
	    win32_job_creation_flags_for_child();

	if (pty == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(pty, 0, sizeof *pty);
	memset(&process, 0, sizeof process);

	if (win32_conpty_init() != 0)
		return (-1);
	if (command == NULL || *command == L'\0')
		command = L"cmd.exe";
	if (columns == 0)
		columns = 80;
	if (rows == 0)
		rows = 24;
	size.X = (SHORT)columns;
	size.Y = (SHORT)rows;

	if (!CreatePipe(&input_read, &input_write, NULL, 0))
		goto fail;
	if (!CreatePipe(&output_read, &output_write, NULL, 0))
		goto fail;
	if (!CreatePipe(&ack_read, &ack_write, &security, 0) ||
	    !SetHandleInformation(ack_read, HANDLE_FLAG_INHERIT, 0))
		goto fail;
	job = CreateJobObjectW(NULL, NULL);
	if (job == NULL)
		goto fail;
	memset(&limits, 0, sizeof limits);
	limits.BasicLimitInformation.LimitFlags =
	    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
	    &limits, sizeof limits))
		goto fail;

	hr = conpty_api.create(size, input_read, output_write, 0,
	    &pseudoconsole);
	if (FAILED(hr)) {
		SetLastError(HRESULT_CODE(hr));
		goto fail;
	}

	CloseHandle(input_read);
	input_read = NULL;
	CloseHandle(output_write);
	output_write = NULL;

	InitializeProcThreadAttributeList(NULL, 2, 0, &attributes_size);
	attributes = HeapAlloc(GetProcessHeap(), 0, attributes_size);
	if (attributes == NULL) {
		SetLastError(ERROR_OUTOFMEMORY);
		goto fail;
	}
	if (!InitializeProcThreadAttributeList(attributes, 2, 0,
	    &attributes_size))
		goto fail;
	attributes_initialized = 1;
	if (!UpdateProcThreadAttribute(attributes, 0,
	    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudoconsole,
	    sizeof pseudoconsole, NULL, NULL))
		goto fail;

	if (!UpdateProcThreadAttribute(attributes, 0,
	    PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &ack_write, sizeof ack_write,
	    NULL, NULL))
		goto fail;
	mutable_command = win32_conpty_child_command(command, ack_write);
	if (mutable_command == NULL) {
		SetLastError(ERROR_OUTOFMEMORY);
		goto fail;
	}

	memset(&startup, 0, sizeof startup);
	startup.StartupInfo.cb = sizeof startup;
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.lpAttributeList = attributes;
	if (environment != NULL)
		creation_flags |= CREATE_UNICODE_ENVIRONMENT;

	if (!CreateProcessW(NULL, mutable_command, NULL, NULL, TRUE,
	    creation_flags, (LPVOID)environment, cwd, &startup.StartupInfo,
	    &process))
		goto fail;
	{
		enum win32_job_assign_result assign_result;
		assign_result = win32_job_assign_or_fallback(job,
		    process.hProcess);
		if (assign_result == WIN32_JOB_ASSIGN_FAILED)
			goto fail;
		if (assign_result == WIN32_JOB_ASSIGN_PARENT_JOB) {
			/*
			 * Process is in a non-breakaway parent job.  Close
			 * our local job so terminate paths use
			 * process-tree + TerminateProcess instead of
			 * TerminateJobObject on an empty job.
			 */
			CloseHandle(job);
			job = NULL;
		}
	}
	if (ResumeThread(process.hThread) == (DWORD)-1)
		goto fail;
	CloseHandle(ack_write);
	ack_write = NULL;
	{
		ULONGLONG deadline = GetTickCount64() + 5000;
		DWORD available, received, child_error;
		for (;;) {
			if (!PeekNamedPipe(ack_read, NULL, 0, NULL, &available, NULL))
				goto fail;
			if (available >= sizeof child_error) break;
			if (GetTickCount64() >= deadline) {
				SetLastError(ERROR_TIMEOUT);
				goto fail;
			}
			Sleep(5);
		}
		if (!ReadFile(ack_read, &child_error, sizeof child_error,
		    &received, NULL) || received != sizeof child_error)
			goto fail;
		if (child_error != ERROR_SUCCESS) {
			SetLastError(child_error);
			goto fail;
		}
	}
	CloseHandle(ack_read);
	ack_read = NULL;

	DeleteProcThreadAttributeList(attributes);
	HeapFree(GetProcessHeap(), 0, attributes);
	free(mutable_command);

	pty->pseudoconsole = pseudoconsole;
	pty->input = input_write;
	pty->output = output_read;
	pty->process = process.hProcess;
	pty->thread = process.hThread;
	pty->job = job;
	pty->process_id = process.dwProcessId;
	return (0);

fail:
	saved_error = GetLastError();
	if (ack_read != NULL) CloseHandle(ack_read);
	if (ack_write != NULL) CloseHandle(ack_write);
	if (process.hProcess != NULL) {
		win32_process_tree_terminate_children(process.dwProcessId,
		    process.hProcess, 1);
		if (job != NULL) TerminateJobObject(job, 1);
		TerminateProcess(process.hProcess, 1);
	}
	if (process.hThread != NULL)
		CloseHandle(process.hThread);
	if (process.hProcess != NULL)
		CloseHandle(process.hProcess);
	if (attributes != NULL) {
		if (attributes_initialized)
			DeleteProcThreadAttributeList(attributes);
		HeapFree(GetProcessHeap(), 0, attributes);
	}
	free(mutable_command);
	if (input_read != NULL)
		CloseHandle(input_read);
	if (input_write != NULL)
		CloseHandle(input_write);
	if (output_read != NULL)
		CloseHandle(output_read);
	if (output_write != NULL)
		CloseHandle(output_write);
	if (job != NULL)
		CloseHandle(job);
	/* No reader exists on spawn failure; close pipes before bounded close. */
	win32_close_pseudoconsole(pseudoconsole);
	SetLastError(saved_error);
	return (-1);
}

int
win32_conpty_resize(struct win32_conpty *pty, unsigned short columns,
    unsigned short rows)
{
	COORD	size;
	HRESULT	hr;

	if (pty == NULL || pty->pseudoconsole == NULL || columns == 0 ||
	    rows == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (win32_conpty_init() != 0)
		return (-1);

	size.X = (SHORT)columns;
	size.Y = (SHORT)rows;
	hr = conpty_api.resize((HPCON)pty->pseudoconsole, size);
	if (FAILED(hr)) {
		SetLastError(HRESULT_CODE(hr));
		return (-1);
	}
	return (0);
}

void
win32_conpty_close(struct win32_conpty *pty)
{
	if (pty == NULL)
		return;

	win32_close_handle(&pty->input);
	win32_close_handle(&pty->output);
	win32_close_handle(&pty->thread);
	win32_close_handle(&pty->process);
	win32_close_pseudoconsole((HPCON)pty->pseudoconsole);
	win32_close_handle(&pty->job);
	memset(pty, 0, sizeof *pty);
}

#endif
