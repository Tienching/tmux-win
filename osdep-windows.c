/* $OpenBSD$ */

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

#include "tmux.h"

#include <tlhelp32.h>
#include <winternl.h>
#include <wchar.h>

#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

struct osdep_win32_curdir {
	UNICODE_STRING	DosPath;
	HANDLE		Handle;
};

struct osdep_win32_process_parameters {
	ULONG				MaximumLength;
	ULONG				Length;
	ULONG				Flags;
	ULONG				DebugFlags;
	HANDLE				ConsoleHandle;
	ULONG				ConsoleFlags;
#ifdef _WIN64
	ULONG				Padding;
#endif
	HANDLE				StandardInput;
	HANDLE				StandardOutput;
	HANDLE				StandardError;
	struct osdep_win32_curdir	CurrentDirectory;
};

typedef NTSTATUS (NTAPI *osdep_win32_ntqueryinformationprocess)(
    HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

static int
osdep_win32_pid_from_tty(const char *tty, DWORD *pid)
{
	char	*end;
	u_long	 value;

	if (tty == NULL || strncmp(tty, "conpty:", 7) != 0)
		return (-1);
	value = strtoul(tty + 7, &end, 10);
	if (*end != '\0' || value == 0 || value > UINT32_MAX)
		return (-1);
	*pid = (DWORD)value;
	return (0);
}

static osdep_win32_ntqueryinformationprocess
osdep_win32_get_ntqueryinformationprocess(void)
{
	HMODULE	module;

	module = GetModuleHandleA("ntdll.dll");
	if (module == NULL)
		return (NULL);
	return ((osdep_win32_ntqueryinformationprocess)
	    GetProcAddress(module, "NtQueryInformationProcess"));
}

static int
osdep_win32_is_console_host(const PROCESSENTRY32W *entry)
{
	return (_wcsicmp(entry->szExeFile, L"conhost.exe") == 0 ||
	    _wcsicmp(entry->szExeFile, L"OpenConsole.exe") == 0);
}

static int
osdep_win32_find_child(HANDLE snapshot, DWORD parent, DWORD *child)
{
	PROCESSENTRY32W	entry;

	memset(&entry, 0, sizeof entry);
	entry.dwSize = sizeof entry;
	if (!Process32FirstW(snapshot, &entry))
		return (0);
	do {
		if (entry.th32ParentProcessID == parent &&
		    entry.th32ProcessID != parent &&
		    !osdep_win32_is_console_host(&entry)) {
			*child = entry.th32ProcessID;
			return (1);
		}
	} while (Process32NextW(snapshot, &entry));
	return (0);
}

static DWORD
osdep_win32_active_pid(DWORD pid)
{
	HANDLE	snapshot;
	DWORD	child;
	u_int	i;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return (pid);

	for (i = 0; i < 64; i++) {
		if (!osdep_win32_find_child(snapshot, pid, &child))
			break;
		pid = child;
	}
	CloseHandle(snapshot);
	return (pid);
}

static char *
osdep_win32_wide_to_utf8(const wchar_t *s, int len)
{
	char	*out;
	int	 size, flags = WC_ERR_INVALID_CHARS;

	if (len <= 0)
		return (NULL);
retry:
	size = WideCharToMultiByte(CP_UTF8, flags, s, len, NULL, 0, NULL, NULL);
	if (size == 0 && flags != 0) {
		flags = 0;
		goto retry;
	}
	if (size == 0)
		return (NULL);

	out = xmalloc((size_t)size + 1);
	if (WideCharToMultiByte(CP_UTF8, flags, s, len, out, size, NULL,
	    NULL) == 0) {
		free(out);
		return (NULL);
	}
	out[size] = '\0';
	return (out);
}

static const wchar_t *
osdep_win32_skip_path_prefix(const wchar_t *path)
{
	if (wcsncmp(path, L"\\\\?\\", 4) == 0 &&
	    wcsncmp(path + 4, L"UNC\\", 4) != 0)
		return (path + 4);
	return (path);
}

static char *
osdep_win32_path_to_utf8(const wchar_t *wide)
{
	const wchar_t	*path;
	size_t		 length;

	path = osdep_win32_skip_path_prefix(wide);
	length = wcslen(path);
	while (length > 3 &&
	    (path[length - 1] == L'\\' || path[length - 1] == L'/'))
		length--;
	if (length == 0 || length > INT_MAX)
		return (NULL);
	return (osdep_win32_wide_to_utf8(path, (int)length));
}

static char *
osdep_win32_cwd_from_handle(HANDLE process, HANDLE remote)
{
	HANDLE	 local = NULL;
	wchar_t	*wide;
	DWORD	 size, needed;
	char	*path;

	if (remote == NULL)
		return (NULL);
	if (!DuplicateHandle(process, remote, GetCurrentProcess(), &local, 0,
	    FALSE, DUPLICATE_SAME_ACCESS))
		return (NULL);

	size = MAX_PATH;
	wide = xcalloc(size, sizeof *wide);
	for (;;) {
		needed = GetFinalPathNameByHandleW(local, wide, size,
		    VOLUME_NAME_DOS);
		if (needed == 0) {
			free(wide);
			CloseHandle(local);
			return (NULL);
		}
		if (needed < size)
			break;
		size = needed + 1;
		wide = xreallocarray(wide, size, sizeof *wide);
	}
	CloseHandle(local);

	path = osdep_win32_path_to_utf8(wide);
	free(wide);
	return (path);
}

static char *
osdep_win32_cwd_from_string(HANDLE process, const UNICODE_STRING *string)
{
	wchar_t	*wide;
	char	*path;
	SIZE_T	 got, length;

	length = string->Length;
	if (length == 0 || string->Buffer == NULL ||
	    (length % sizeof *wide) != 0)
		return (NULL);
	wide = xcalloc((length / sizeof *wide) + 1, sizeof *wide);
	if (!ReadProcessMemory(process, string->Buffer, wide, length, &got) ||
	    got != length) {
		free(wide);
		return (NULL);
	}
	path = osdep_win32_path_to_utf8(wide);
	free(wide);
	return (path);
}

static char *
osdep_win32_get_name_from_pid(DWORD pid)
{
	char	path[MAX_PATH];
	DWORD	size;
	HANDLE	process;

	process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == NULL)
		return (NULL);
	size = sizeof path;
	if (!QueryFullProcessImageNameA(process, 0, path, &size)) {
		CloseHandle(process);
		return (NULL);
	}
	CloseHandle(process);
	path[sizeof path - 1] = '\0';
	return (xstrdup(path));
}

char *
osdep_get_name(__unused int fd, char *tty)
{
	char	*path;
	DWORD	 pid, active;

	if (osdep_win32_pid_from_tty(tty, &pid) != 0)
		return (NULL);

	active = osdep_win32_active_pid(pid);
	path = osdep_win32_get_name_from_pid(active);
	if (path != NULL || active == pid)
		return (path);
	return (osdep_win32_get_name_from_pid(pid));
}

char *
osdep_get_cwd(__unused int fd)
{
	return (NULL);
}

static char *
osdep_win32_get_cwd_from_pid(DWORD pid)
{
	PROCESS_BASIC_INFORMATION		 pbi;
	PEB					 peb;
	struct osdep_win32_process_parameters	 params;
	osdep_win32_ntqueryinformationprocess	 ntquery;
	HANDLE					 process;
	char					*path;
	SIZE_T					 got;
	NTSTATUS				 status;

	ntquery = osdep_win32_get_ntqueryinformationprocess();
	if (ntquery == NULL)
		return (NULL);

	process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
	    PROCESS_VM_READ|PROCESS_DUP_HANDLE, FALSE, pid);
	if (process == NULL)
		return (NULL);

	memset(&pbi, 0, sizeof pbi);
	status = ntquery(process, ProcessBasicInformation, &pbi, sizeof pbi,
	    NULL);
	if (!NT_SUCCESS(status) || pbi.PebBaseAddress == NULL)
		goto fail;
	if (!ReadProcessMemory(process, pbi.PebBaseAddress, &peb, sizeof peb,
	    &got) || got != sizeof peb || peb.ProcessParameters == NULL)
		goto fail;
	if (!ReadProcessMemory(process, peb.ProcessParameters, &params,
	    sizeof params, &got) || got != sizeof params)
		goto fail;

	path = osdep_win32_cwd_from_string(process,
	    &params.CurrentDirectory.DosPath);
	if (path != NULL) {
		CloseHandle(process);
		return (path);
	}

	path = osdep_win32_cwd_from_handle(process,
	    params.CurrentDirectory.Handle);
	if (path != NULL) {
		CloseHandle(process);
		return (path);
	}

fail:
	CloseHandle(process);
	return (NULL);
}

char *
osdep_get_cwd_from_tty(const char *tty)
{
	char	*path;
	DWORD	 pid, active;

	if (osdep_win32_pid_from_tty(tty, &pid) != 0)
		return (NULL);

	active = osdep_win32_active_pid(pid);
	path = osdep_win32_get_cwd_from_pid(active);
	if (path != NULL || active == pid)
		return (path);
	return (osdep_win32_get_cwd_from_pid(pid));
}

struct event_base *
osdep_event_init(void)
{
	WSADATA	data;

	if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
		fatalx("WSAStartup failed");
	return (event_init());
}
