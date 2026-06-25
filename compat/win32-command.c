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

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "win32-command.h"

struct win32_wide_buffer {
	wchar_t		*data;
	size_t		 len;
	size_t		 cap;
};

static int
win32_buffer_reserve(struct win32_wide_buffer *buffer, size_t add)
{
	wchar_t	*new_data;
	size_t	 new_cap;

	if (add > SIZE_MAX - buffer->len - 1) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	if (buffer->len + add + 1 <= buffer->cap)
		return (0);

	new_cap = buffer->cap;
	if (new_cap == 0)
		new_cap = 64;
	while (new_cap < buffer->len + add + 1) {
		if (new_cap > SIZE_MAX / 2 / sizeof *buffer->data) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			return (-1);
		}
		new_cap *= 2;
	}

	new_data = realloc(buffer->data, new_cap * sizeof *buffer->data);
	if (new_data == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	buffer->data = new_data;
	buffer->cap = new_cap;
	return (0);
}

static int
win32_buffer_append_char(struct win32_wide_buffer *buffer, wchar_t ch)
{
	if (win32_buffer_reserve(buffer, 1) != 0)
		return (-1);
	buffer->data[buffer->len++] = ch;
	buffer->data[buffer->len] = L'\0';
	return (0);
}

static int
win32_buffer_append_chars(struct win32_wide_buffer *buffer, wchar_t ch,
    size_t count)
{
	if (win32_buffer_reserve(buffer, count) != 0)
		return (-1);
	while (count-- != 0)
		buffer->data[buffer->len++] = ch;
	buffer->data[buffer->len] = L'\0';
	return (0);
}

static int
win32_buffer_append_string(struct win32_wide_buffer *buffer, const wchar_t *s)
{
	size_t	len;

	len = wcslen(s);
	if (win32_buffer_reserve(buffer, len) != 0)
		return (-1);
	memcpy(buffer->data + buffer->len, s, len * sizeof *s);
	buffer->len += len;
	buffer->data[buffer->len] = L'\0';
	return (0);
}

static int
win32_arg_needs_quotes(const wchar_t *arg)
{
	if (*arg == L'\0')
		return (1);
	for (; *arg != L'\0'; arg++) {
		if (*arg == L' ' || *arg == L'\t' || *arg == L'\n' ||
		    *arg == L'\v' || *arg == L'"')
			return (1);
	}
	return (0);
}

static int
win32_buffer_append_quoted_arg(struct win32_wide_buffer *buffer,
    const wchar_t *arg)
{
	size_t	backslashes = 0;

	if (!win32_arg_needs_quotes(arg))
		return (win32_buffer_append_string(buffer, arg));

	if (win32_buffer_append_char(buffer, L'"') != 0)
		return (-1);

	for (; *arg != L'\0'; arg++) {
		if (*arg == L'\\') {
			backslashes++;
			continue;
		}
		if (*arg == L'"') {
			if (win32_buffer_append_chars(buffer, L'\\',
			    backslashes * 2 + 1) != 0)
				return (-1);
			if (win32_buffer_append_char(buffer, L'"') != 0)
				return (-1);
			backslashes = 0;
			continue;
		}
		if (backslashes != 0) {
			if (win32_buffer_append_chars(buffer, L'\\',
			    backslashes) != 0)
				return (-1);
			backslashes = 0;
		}
		if (win32_buffer_append_char(buffer, *arg) != 0)
			return (-1);
	}

	if (backslashes != 0 &&
	    win32_buffer_append_chars(buffer, L'\\', backslashes * 2) != 0)
		return (-1);
	return (win32_buffer_append_char(buffer, L'"'));
}

static wchar_t *
win32_build_cmd_command_line_wide(const wchar_t *const *argv)
{
	struct win32_wide_buffer	 buffer;
	int			 i;

	memset(&buffer, 0, sizeof buffer);
	for (i = 0; i < 3; i++) {
		if (i != 0 && win32_buffer_append_char(&buffer, L' ') != 0)
			goto fail;
		if (win32_buffer_append_quoted_arg(&buffer, argv[i]) != 0)
			goto fail;
	}
	if (win32_buffer_append_char(&buffer, L' ') != 0)
		goto fail;

	/*
	 * cmd.exe parses the command after /c or /k itself; CRT-style escaping
	 * turns embedded quotes into literal backslash-quote pairs and breaks
	 * redirection targets with quoted Windows paths.
	 */
	if (win32_buffer_append_string(&buffer, argv[3]) != 0)
		goto fail;
	return (buffer.data);

fail:
	free(buffer.data);
	return (NULL);
}

wchar_t *
win32_utf8_to_wide(const char *s)
{
	wchar_t	*out;
	int	 size;

	if (s == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1,
	    NULL, 0);
	if (size == 0)
		return (NULL);

	out = malloc(size * sizeof *out);
	if (out == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out,
	    size) == 0) {
		free(out);
		return (NULL);
	}
	return (out);
}

static wchar_t *
win32_wcsdup_local(const wchar_t *s)
{
	wchar_t	*copy;
	size_t	 len;

	len = wcslen(s) + 1;
	copy = malloc(len * sizeof *copy);
	if (copy == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	memcpy(copy, s, len * sizeof *copy);
	return (copy);
}

static int
win32_wide_path_is_drive_absolute(const wchar_t *path)
{
	wchar_t	ch;

	ch = path[0];
	if (!((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z')))
		return (0);
	if (path[1] != L':' || path[2] != L'\\')
		return (0);
	return (1);
}

static wchar_t *
win32_wide_to_extended_path(const wchar_t *path)
{
	static const wchar_t	 drive_prefix[] = L"\\\\?\\";
	static const wchar_t	 unc_prefix[] = L"\\\\?\\UNC\\";
	wchar_t			*extended;
	size_t			 prefix_len, path_len, tail_offset;

	if (path == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}
	if (wcsncmp(path, L"\\\\?\\", 4) == 0)
		return (win32_wcsdup_local(path));
	if (wcslen(path) < MAX_PATH)
		return (win32_wcsdup_local(path));
	if (path[0] == L'\\' && path[1] == L'\\') {
		prefix_len = wcslen(unc_prefix);
		tail_offset = 2;
	} else if (win32_wide_path_is_drive_absolute(path)) {
		prefix_len = wcslen(drive_prefix);
		tail_offset = 0;
	} else
		return (win32_wcsdup_local(path));

	path_len = wcslen(path + tail_offset);
	if (path_len > SIZE_MAX / sizeof *extended - prefix_len - 1) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	extended = malloc((prefix_len + path_len + 1) * sizeof *extended);
	if (extended == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	if (tail_offset == 0)
		memcpy(extended, drive_prefix, prefix_len * sizeof *extended);
	else
		memcpy(extended, unc_prefix, prefix_len * sizeof *extended);
	memcpy(extended + prefix_len, path + tail_offset,
	    (path_len + 1) * sizeof *extended);
	return (extended);
}

wchar_t *
win32_utf8_to_wide_path(const char *s)
{
	wchar_t	*wide, *extended;

	wide = win32_utf8_to_wide(s);
	if (wide == NULL)
		return (NULL);
	extended = win32_wide_to_extended_path(wide);
	free(wide);
	return (extended);
}

char *
win32_wide_to_utf8(const wchar_t *wide)
{
	int	 n;
	int	 flags = WC_ERR_INVALID_CHARS;
	char	*utf8;

	if (wide == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}
retry:
	n = WideCharToMultiByte(CP_UTF8, flags, wide, -1, NULL, 0, NULL,
	    NULL);
	if (n <= 0 && flags != 0) {
		flags = 0;
		goto retry;
	}
	if (n <= 0)
		return (NULL);
	utf8 = calloc((size_t)n, sizeof *utf8);
	if (utf8 == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	if (WideCharToMultiByte(CP_UTF8, flags, wide, -1, utf8, n, NULL,
	    NULL) == 0) {
		free(utf8);
		return (NULL);
	}
	return (utf8);
}

char *
win32_wide_to_utf8_path(const wchar_t *wide)
{
	return (win32_wide_to_utf8(wide));
}

wchar_t *
win32_build_command_line_wide(int argc, const wchar_t *const *argv)
{
	struct win32_wide_buffer	 buffer;
	int			 i;

	if (argc <= 0 || argv == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	memset(&buffer, 0, sizeof buffer);
	for (i = 0; i < argc; i++) {
		if (argv[i] == NULL) {
			free(buffer.data);
			SetLastError(ERROR_INVALID_PARAMETER);
			return (NULL);
		}
		if (i != 0 && win32_buffer_append_char(&buffer, L' ') != 0)
			goto fail;
		if (win32_buffer_append_quoted_arg(&buffer, argv[i]) != 0)
			goto fail;
	}
	return (buffer.data);

fail:
	free(buffer.data);
	return (NULL);
}

wchar_t *
win32_build_command_line(int argc, char *const *argv)
{
	wchar_t	**wide_argv, *command_line;
	int	  i;

	if (argc <= 0 || argv == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	wide_argv = calloc(argc, sizeof *wide_argv);
	if (wide_argv == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	for (i = 0; i < argc; i++) {
		wide_argv[i] = win32_utf8_to_wide(argv[i]);
		if (wide_argv[i] == NULL)
			goto fail;
	}

	command_line = win32_build_command_line_wide(argc,
	    (const wchar_t *const *)wide_argv);
	if (argc == 4 && win32_shell_is_cmd(argv[0]) &&
	    _stricmp(argv[1], "/d") == 0 &&
	    (_stricmp(argv[2], "/c") == 0 ||
	    _stricmp(argv[2], "/k") == 0)) {
		free(command_line);
		command_line = win32_build_cmd_command_line_wide(
		    (const wchar_t *const *)wide_argv);
	}
	for (i = 0; i < argc; i++)
		free(wide_argv[i]);
	free(wide_argv);
	return (command_line);

fail:
	for (i = 0; i < argc; i++)
		free(wide_argv[i]);
	free(wide_argv);
	return (NULL);
}

int
win32_shell_is_cmd(const char *shell)
{
	const char	*base;

	base = strrchr(shell, '\\');
	if (base == NULL)
		base = strrchr(shell, '/');
	if (base != NULL)
		base++;
	else
		base = shell;
	return (_stricmp(base, "cmd.exe") == 0 || _stricmp(base, "cmd") == 0);
}

int
win32_shell_command_argv(const char *shell, const char *cmd, char **argv)
{
	argv[0] = (char *)shell;
	if (win32_shell_is_cmd(shell)) {
		argv[1] = "/d";
		argv[2] = "/c";
		argv[3] = (char *)cmd;
		return (4);
	}
	argv[1] = "-c";
	argv[2] = (char *)cmd;
	return (3);
}

#endif
