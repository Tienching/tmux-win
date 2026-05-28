/* $OpenBSD$ */
/*
 * Copyright (c) 2026 tmux Windows Port Contributors
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

#include "win32-endpoint.h"

#include <sddl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Internal helpers (T-002, design.md sec 2.3). Kept in this translation
 * unit and prefixed with win32ep_.
 */

static int
win32ep_utf8_to_wide(const char *src, wchar_t **dst)
{
	int	n;
	wchar_t	*buf;

	if (src == NULL || dst == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*dst = NULL;
	n = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
	if (n <= 0)
		return (-1);
	buf = calloc((size_t)n, sizeof *buf);
	if (buf == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	if (MultiByteToWideChar(CP_UTF8, 0, src, -1, buf, n) <= 0) {
		free(buf);
		return (-1);
	}
	*dst = buf;
	return (0);
}

static int
win32ep_current_user_sid(wchar_t out[WIN32_ENDPOINT_SID_MAX])
{
	HANDLE	token = NULL;
	DWORD	needed = 0;
	TOKEN_USER *user = NULL;
	wchar_t	*sid_str = NULL;
	int	rc = -1;

	out[0] = L'\0';
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		goto out;
	GetTokenInformation(token, TokenUser, NULL, 0, &needed);
	if (needed == 0)
		goto out;
	user = malloc(needed);
	if (user == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (!GetTokenInformation(token, TokenUser, user, needed, &needed))
		goto out;
	if (!ConvertSidToStringSidW(user->User.Sid, &sid_str))
		goto out;
	if (wcslen(sid_str) >= WIN32_ENDPOINT_SID_MAX) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	wcscpy_s(out, WIN32_ENDPOINT_SID_MAX, sid_str);
	rc = 0;

out:
	if (sid_str != NULL)
		LocalFree(sid_str);
	free(user);
	if (token != NULL)
		CloseHandle(token);
	return (rc);
}

static int
win32ep_local_appdata(wchar_t **out)
{
	wchar_t	*folder = NULL;
	HRESULT	hr;

	hr = SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &folder);
	if (FAILED(hr) || folder == NULL) {
		if (folder != NULL)
			CoTaskMemFree(folder);
		SetLastError(ERROR_PATH_NOT_FOUND);
		return (-1);
	}
	*out = _wcsdup(folder);
	CoTaskMemFree(folder);
	if (*out == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	return (0);
}

static int
win32ep_create_directories(const wchar_t *path)
{
	wchar_t	buf[MAX_PATH * 2];
	size_t	i, len;

	len = wcslen(path);
	if (len + 1 > sizeof buf / sizeof *buf) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}
	wcscpy_s(buf, sizeof buf / sizeof *buf, path);
	for (i = 1; i < len; i++) {
		if (buf[i] != L'\\' && buf[i] != L'/')
			continue;
		buf[i] = L'\0';
		if (!CreateDirectoryW(buf, NULL) &&
		    GetLastError() != ERROR_ALREADY_EXISTS)
			return (-1);
		buf[i] = L'\\';
	}
	if (!CreateDirectoryW(buf, NULL) &&
	    GetLastError() != ERROR_ALREADY_EXISTS)
		return (-1);
	return (0);
}

int
win32_endpoint_resolve_path(const char *socket_name, wchar_t **path_out)
{
	wchar_t	*appdata = NULL, *socket_w = NULL, *full = NULL;
	wchar_t	sid[WIN32_ENDPOINT_SID_MAX];
	wchar_t	dir[MAX_PATH * 2];
	size_t	cap;
	int	rc = -1;

	if (socket_name == NULL || path_out == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*path_out = NULL;
	if (win32ep_local_appdata(&appdata) != 0)
		goto out;
	if (win32ep_current_user_sid(sid) != 0)
		goto out;
	if (win32ep_utf8_to_wide(socket_name, &socket_w) != 0)
		goto out;

	if (swprintf_s(dir, sizeof dir / sizeof *dir, L"%ls\\tmux\\%ls",
	    appdata, sid) < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	if (win32ep_create_directories(dir) != 0)
		goto out;

	cap = wcslen(dir) + wcslen(socket_w) + 16;
	full = calloc(cap, sizeof *full);
	if (full == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (swprintf_s(full, cap, L"%ls\\%ls.endpoint", dir, socket_w) < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	*path_out = full;
	full = NULL;
	rc = 0;

out:
	free(appdata);
	free(socket_w);
	free(full);
	return (rc);
}

int
win32_endpoint_format_pipe_names(const char *socket_name,
    wchar_t pipe_ctl[WIN32_ENDPOINT_PIPE_MAX],
    wchar_t pipe_evt[WIN32_ENDPOINT_PIPE_MAX])
{
	wchar_t	sid[WIN32_ENDPOINT_SID_MAX];
	wchar_t	*socket_w = NULL;
	int	rc = -1;

	if (socket_name == NULL || pipe_ctl == NULL || pipe_evt == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (win32ep_current_user_sid(sid) != 0)
		goto out;
	if (win32ep_utf8_to_wide(socket_name, &socket_w) != 0)
		goto out;
	if (swprintf_s(pipe_ctl, WIN32_ENDPOINT_PIPE_MAX,
	    L"\\\\.\\pipe\\Local\\tmux-%ls-%ls-ctl", sid, socket_w) < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	if (swprintf_s(pipe_evt, WIN32_ENDPOINT_PIPE_MAX,
	    L"\\\\.\\pipe\\Local\\tmux-%ls-%ls-evt", sid, socket_w) < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		goto out;
	}
	rc = 0;

out:
	free(socket_w);
	return (rc);
}

int
win32_endpoint_write_atomic(const wchar_t *path,
    const struct win32_endpoint_record *record)
{
	wchar_t	tmp_path[MAX_PATH * 2];
	wchar_t	buffer[2048];
	HANDLE	file = INVALID_HANDLE_VALUE;
	int	written_chars;
	DWORD	bytes_written = 0;
	DWORD	bytes_total;
	int	rc = -1;

	if (path == NULL || record == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (record->version <= 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (swprintf_s(tmp_path, sizeof tmp_path / sizeof *tmp_path,
	    L"%ls.tmp", path) < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}

	written_chars = swprintf_s(buffer, sizeof buffer / sizeof *buffer,
	    L"%d\n%ls\n%lu\n%ls\n%ls\n%ls\n%ls\n",
	    record->version, record->sid, (unsigned long)record->pid,
	    record->pipe_ctl, record->pipe_evt, record->started_at,
	    record->tmux_version);
	if (written_chars < 0) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}
	bytes_total = (DWORD)(written_chars * sizeof(wchar_t));

	DeleteFileW(tmp_path);
	file = CreateFileW(tmp_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
	    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return (-1);

	/* Write a UTF-16 LE BOM so future readers can self-identify. */
	{
		const unsigned char bom[2] = { 0xFF, 0xFE };
		DWORD bom_written = 0;

		if (!WriteFile(file, bom, sizeof bom, &bom_written, NULL) ||
		    bom_written != sizeof bom)
			goto out;
	}
	if (!WriteFile(file, buffer, bytes_total, &bytes_written, NULL) ||
	    bytes_written != bytes_total)
		goto out;
	if (!FlushFileBuffers(file))
		goto out;
	CloseHandle(file);
	file = INVALID_HANDLE_VALUE;

	if (!MoveFileExW(tmp_path, path,
	    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		goto out;
	rc = 0;

out:
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	if (rc != 0)
		DeleteFileW(tmp_path);
	return (rc);
}

int
win32_endpoint_read(const wchar_t *path, struct win32_endpoint_record *record)
{
	HANDLE		file = INVALID_HANDLE_VALUE;
	LARGE_INTEGER	size;
	wchar_t		*buffer = NULL;
	DWORD		read_bytes = 0;
	int		rc = -1;
	int		consumed;
	int		version_in;
	unsigned long	pid_in;
	wchar_t		sid_in[WIN32_ENDPOINT_SID_MAX];
	wchar_t		pipe_ctl_in[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t		pipe_evt_in[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t		started_in[WIN32_ENDPOINT_TIME_MAX];
	wchar_t		ver_in[WIN32_ENDPOINT_VERSION_STR_MAX];

	if (path == NULL || record == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(record, 0, sizeof *record);

	file = CreateFileW(path, GENERIC_READ,
	    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
	    FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return (-1);
	if (!GetFileSizeEx(file, &size) || size.QuadPart < 4 ||
	    size.QuadPart > 8192) {
		SetLastError(ERROR_BAD_FORMAT);
		goto out;
	}
	buffer = calloc((size_t)size.QuadPart + sizeof(wchar_t), 1);
	if (buffer == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &read_bytes, NULL))
		goto out;

	{
		/* Strip optional UTF-16 LE BOM. */
		wchar_t *cursor = buffer;
		unsigned char *raw = (unsigned char *)buffer;
		if (read_bytes >= 2 && raw[0] == 0xFF && raw[1] == 0xFE)
			cursor = (wchar_t *)(raw + 2);
		consumed = swscanf_s(cursor,
		    L"%d\n%255l[^\n]\n%lu\n%259l[^\n]\n%259l[^\n]\n"
		    L"%31l[^\n]\n%63l[^\n]",
		    &version_in,
		    sid_in, (unsigned)WIN32_ENDPOINT_SID_MAX,
		    &pid_in,
		    pipe_ctl_in, (unsigned)WIN32_ENDPOINT_PIPE_MAX,
		    pipe_evt_in, (unsigned)WIN32_ENDPOINT_PIPE_MAX,
		    started_in, (unsigned)WIN32_ENDPOINT_TIME_MAX,
		    ver_in, (unsigned)WIN32_ENDPOINT_VERSION_STR_MAX);
	}
	if (consumed != 7) {
		SetLastError(ERROR_BAD_FORMAT);
		goto out;
	}

	record->version = version_in;
	wcscpy_s(record->sid, WIN32_ENDPOINT_SID_MAX, sid_in);
	record->pid = (DWORD)pid_in;
	wcscpy_s(record->pipe_ctl, WIN32_ENDPOINT_PIPE_MAX, pipe_ctl_in);
	wcscpy_s(record->pipe_evt, WIN32_ENDPOINT_PIPE_MAX, pipe_evt_in);
	wcscpy_s(record->started_at, WIN32_ENDPOINT_TIME_MAX, started_in);
	wcscpy_s(record->tmux_version, WIN32_ENDPOINT_VERSION_STR_MAX, ver_in);
	rc = 0;

out:
	if (file != INVALID_HANDLE_VALUE)
		CloseHandle(file);
	free(buffer);
	return (rc);
}

int
win32_endpoint_unlink(const wchar_t *path)
{
	if (path == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (!DeleteFileW(path)) {
		if (GetLastError() == ERROR_FILE_NOT_FOUND)
			return (0);
		return (-1);
	}
	return (0);
}

#endif /* _WIN32 */
