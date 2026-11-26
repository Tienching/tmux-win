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
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

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
static struct win32_conpty_api bundled_conpty_api, system_conpty_api;

struct win32_conpty_trust {
	union {
		FARPROC proc;
		LONG (WINAPI *call)(HWND, GUID *, LPVOID);
	} verify;
	union {
		FARPROC proc;
		CRYPT_PROVIDER_DATA *(WINAPI *call)(HANDLE);
	} provider;
	union {
		FARPROC proc;
		CRYPT_PROVIDER_SGNR *(WINAPI *call)(CRYPT_PROVIDER_DATA *,
		    DWORD, BOOL, DWORD);
	} signer;
	union {
		FARPROC proc;
		BOOL (WINAPI *call)(LPCSTR, PCCERT_CHAIN_CONTEXT,
		    PCERT_CHAIN_POLICY_PARA, PCERT_CHAIN_POLICY_STATUS);
	} chain_policy;
};

static int
win32_conpty_verify_file(const struct win32_conpty_trust *trust,
    const wchar_t *path, HANDLE handle)
{
	GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	WINTRUST_FILE_INFO file = {0};
	WINTRUST_DATA data = {0};
	CERT_CHAIN_POLICY_PARA policy = {0};
	CERT_CHAIN_POLICY_STATUS status = {0};
	CRYPT_PROVIDER_DATA *provider;
	CRYPT_PROVIDER_SGNR *signer;
	int valid = 0;

	file.cbStruct = sizeof file;
	file.pcwszFilePath = path;
	file.hFile = handle;
	data.cbStruct = sizeof data;
	data.dwUIChoice = WTD_UI_NONE;
	data.fdwRevocationChecks = WTD_REVOKE_NONE;
	data.dwUnionChoice = WTD_CHOICE_FILE;
	data.pFile = &file;
	data.dwStateAction = WTD_STATEACTION_VERIFY;
	data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_DISABLE_MD2_MD4;
	if (trust->verify.call(INVALID_HANDLE_VALUE, &action, &data) != 0)
		goto done;

	/* Check the chain of the signer that passed Authenticode verification. */
	provider = trust->provider.call(data.hWVTStateData);
	if (provider == NULL)
		goto done;
	signer = trust->signer.call(provider, 0, FALSE, 0);
	if (signer == NULL || signer->pChainContext == NULL)
		goto done;
	policy.cbSize = sizeof policy;
	policy.dwFlags =
	    MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG;
	status.cbSize = sizeof status;
	valid = trust->chain_policy.call(CERT_CHAIN_POLICY_MICROSOFT_ROOT,
	    signer->pChainContext, &policy, &status) && status.dwError == 0;

done:
	data.dwStateAction = WTD_STATEACTION_CLOSE;
	trust->verify.call(INVALID_HANDLE_VALUE, &action, &data);
	return (valid);
}

static int
win32_conpty_load_api(HMODULE module, int bundled, struct win32_conpty_api *api)
{
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

	create.proc = GetProcAddress(module, bundled ? "ConptyCreatePseudoConsole" :
	    "CreatePseudoConsole");
	resize.proc = GetProcAddress(module, bundled ? "ConptyResizePseudoConsole" :
	    "ResizePseudoConsole");
	close.proc = GetProcAddress(module, bundled ? "ConptyClosePseudoConsole" :
	    "ClosePseudoConsole");
	if (create.proc == NULL || resize.proc == NULL || close.proc == NULL)
		return (-1);
	api->create = create.create;
	api->resize = resize.resize;
	api->close = close.close;
	return (0);
}

static void
win32_conpty_load_bundled(void)
{
	static const wchar_t host[] = L"conpty\\OpenConsole.exe";
	static const wchar_t library[] = L"conpty\\conpty.dll";
	struct win32_conpty_trust trust;
	wchar_t host_path[32768], library_path[32768], *filename;
	DWORD length;
	size_t prefix;
	HMODULE wintrust, crypt32, module = NULL;
	HANDLE host_file = INVALID_HANDLE_VALUE, dll_file = INVALID_HANDLE_VALUE;
	int loaded = 0;

	length = GetModuleFileNameW(NULL, host_path, _countof(host_path));
	if (length == 0 || length >= _countof(host_path))
		return;
	filename = wcsrchr(host_path, L'\\');
	if (filename == NULL)
		return;
	prefix = (size_t)(++filename - host_path);
	if (prefix + _countof(host) > _countof(host_path))
		return;
	memcpy(library_path, host_path, prefix * sizeof *filename);
	memcpy(filename, host, sizeof host);
	memcpy(library_path + prefix, library, sizeof library);

	/* Keep both files open without write or delete sharing while verifying. */
	host_file = CreateFileW(host_path, GENERIC_READ, FILE_SHARE_READ, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (host_file == INVALID_HANDLE_VALUE)
		return;
	dll_file = CreateFileW(library_path, GENERIC_READ, FILE_SHARE_READ, NULL,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (dll_file == INVALID_HANDLE_VALUE) {
		CloseHandle(host_file);
		return;
	}

	wintrust = LoadLibraryExW(L"wintrust.dll", NULL,
	    LOAD_LIBRARY_SEARCH_SYSTEM32);
	crypt32 = LoadLibraryExW(L"crypt32.dll", NULL,
	    LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (wintrust == NULL || crypt32 == NULL)
		goto done;
	trust.verify.proc = GetProcAddress(wintrust, "WinVerifyTrust");
	trust.provider.proc = GetProcAddress(wintrust,
	    "WTHelperProvDataFromStateData");
	trust.signer.proc = GetProcAddress(wintrust,
	    "WTHelperGetProvSignerFromChain");
	trust.chain_policy.proc = GetProcAddress(crypt32,
	    "CertVerifyCertificateChainPolicy");
	if (trust.verify.proc == NULL || trust.provider.proc == NULL ||
	    trust.signer.proc == NULL || trust.chain_policy.proc == NULL)
		goto done;
	if (!win32_conpty_verify_file(&trust, host_path, host_file) ||
	    !win32_conpty_verify_file(&trust, library_path, dll_file))
		goto done;
	module = LoadLibraryExW(library_path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (module != NULL)
		loaded = (win32_conpty_load_api(module, 1, &bundled_conpty_api) == 0);

done:
	if (!loaded && module != NULL)
		FreeLibrary(module);
	if (crypt32 != NULL)
		FreeLibrary(crypt32);
	if (wintrust != NULL)
		FreeLibrary(wintrust);
	CloseHandle(dll_file);
	/* The bundled DLL launches this host by path for each new pane. */
	if (!loaded)
		CloseHandle(host_file);
}

static BOOL CALLBACK
win32_conpty_init_once(__unused PINIT_ONCE once, __unused PVOID parameter,
    __unused PVOID *context)
{
	HMODULE module;

	module = GetModuleHandleW(L"kernel32.dll");
	if (module != NULL)
		win32_conpty_load_api(module, 0, &system_conpty_api);
	win32_conpty_load_bundled();
	return (bundled_conpty_api.create != NULL ||
	    system_conpty_api.create != NULL);
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
	const struct win32_conpty_api *api;
	HPCON	pseudoconsole;
};

static DWORD WINAPI
win32_close_pseudoconsole_thread(LPVOID data0)
{
	struct win32_close_pseudoconsole_data	*data = data0;

	data->api->close(data->pseudoconsole);
	free(data);
	return (0);
}

static void
win32_close_pseudoconsole(const struct win32_conpty_api *api,
    HPCON pseudoconsole)
{
	struct win32_close_pseudoconsole_data	*data;
	HANDLE					 thread;

	if (pseudoconsole == NULL)
		return;

	data = malloc(sizeof *data);
	if (data == NULL)
		return;
	data->api = api;
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

static int
win32_conpty_spawn_with_api(struct win32_conpty *pty,
    const struct win32_conpty_api *api, const wchar_t *command,
    const wchar_t *cwd, const wchar_t *environment, unsigned short columns,
    unsigned short rows, int *create_failed)
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

	*create_failed = 0;
	memset(&process, 0, sizeof process);

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

	hr = api->create(size, input_read, output_write, 0, &pseudoconsole);
	if (FAILED(hr)) {
		*create_failed = 1;
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

	pty->api = api;
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
	win32_close_pseudoconsole(api, pseudoconsole);
	SetLastError(saved_error);
	return (-1);
}

int
win32_conpty_spawn(struct win32_conpty *pty, const wchar_t *command,
    const wchar_t *cwd, const wchar_t *environment, unsigned short columns,
    unsigned short rows)
{
	int create_failed;

	if (pty == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(pty, 0, sizeof *pty);
	if (win32_conpty_init() != 0)
		return (-1);

	if (bundled_conpty_api.create != NULL) {
		if (win32_conpty_spawn_with_api(pty, &bundled_conpty_api, command,
		    cwd, environment, columns, rows, &create_failed) == 0)
			return (0);
		/* A failure after allocation must not start the command again. */
		if (!create_failed || system_conpty_api.create == NULL)
			return (-1);
	}
	return (win32_conpty_spawn_with_api(pty, &system_conpty_api, command,
	    cwd, environment, columns, rows, &create_failed));
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
	size.X = (SHORT)columns;
	size.Y = (SHORT)rows;
	hr = pty->api->resize((HPCON)pty->pseudoconsole, size);
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
	win32_close_pseudoconsole(pty->api, (HPCON)pty->pseudoconsole);
	win32_close_handle(&pty->job);
	memset(pty, 0, sizeof *pty);
}

#endif
