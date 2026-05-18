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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "win32-clipboard.h"

static int
win32_clipboard_open(void)
{
	DWORD	last = ERROR_SUCCESS;
	int	i;

	for (i = 0; i < 10; i++) {
		if (OpenClipboard(NULL))
			return (0);
		last = GetLastError();
		Sleep(100);
	}
	SetLastError(last);
	return (-1);
}

static wchar_t *
win32_clipboard_utf8_to_text(const char *buf, size_t len, size_t *outlen)
{
	wchar_t	*wide, *text;
	int	 wide_len;
	size_t	 i, j, extra = 0;

	if (len > INT_MAX) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}
	if (memchr(buf, '\0', len) != NULL) {
		SetLastError(ERROR_INVALID_DATA);
		return (NULL);
	}

	if (len == 0) {
		text = calloc(1, sizeof *text);
		if (text == NULL)
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		else
			*outlen = 0;
		return (text);
	}

	wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf,
	    (int)len, NULL, 0);
	if (wide_len == 0)
		return (NULL);
	wide = malloc((size_t)wide_len * sizeof *wide);
	if (wide == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, (int)len,
	    wide, wide_len) == 0) {
		free(wide);
		return (NULL);
	}

	for (i = 0; i < (size_t)wide_len; i++) {
		if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r'))
			extra++;
	}
	if ((size_t)wide_len > SIZE_MAX - extra - 1) {
		free(wide);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	text = malloc(((size_t)wide_len + extra + 1) * sizeof *text);
	if (text == NULL) {
		free(wide);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}

	for (i = j = 0; i < (size_t)wide_len; i++) {
		if (wide[i] == L'\n' && (i == 0 || wide[i - 1] != L'\r'))
			text[j++] = L'\r';
		text[j++] = wide[i];
	}
	text[j] = L'\0';
	*outlen = j;
	free(wide);
	return (text);
}

int
win32_clipboard_set_text(const char *buf, size_t len)
{
	HGLOBAL	 memory = NULL;
	wchar_t	*text, *locked;
	size_t	 textlen, bytes;
	int	 retval = -1;

	if (buf == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	text = win32_clipboard_utf8_to_text(buf, len, &textlen);
	if (text == NULL)
		return (-1);
	if (textlen > (SIZE_MAX / sizeof *text) - 1) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	bytes = (textlen + 1) * sizeof *text;

	memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (memory == NULL)
		goto out;
	locked = GlobalLock(memory);
	if (locked == NULL)
		goto out;
	memcpy(locked, text, bytes);
	GlobalUnlock(memory);

	if (win32_clipboard_open() != 0)
		goto out;
	if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory) != NULL) {
		memory = NULL;
		retval = 0;
	}
	CloseClipboard();

out:
	if (memory != NULL)
		GlobalFree(memory);
	free(text);
	return (retval);
}

static char *
win32_clipboard_text_to_utf8(const wchar_t *text, size_t *outlen)
{
	wchar_t	*normalized;
	char	*out;
	size_t	 i, j, len;
	int	 bytes;

	len = wcslen(text);
	if (len == 0) {
		out = malloc(1);
		if (out == NULL)
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		else
			*outlen = 0;
		return (out);
	}
	if (len > INT_MAX) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	normalized = malloc((len + 1) * sizeof *normalized);
	if (normalized == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	for (i = j = 0; i < len; i++) {
		if (text[i] == L'\r' && i + 1 < len && text[i + 1] == L'\n')
			continue;
		normalized[j++] = text[i];
	}
	normalized[j] = L'\0';
	if (j == 0) {
		free(normalized);
		out = malloc(1);
		if (out == NULL)
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		else
			*outlen = 0;
		return (out);
	}
	if (j > INT_MAX) {
		free(normalized);
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized,
	    (int)j, NULL, 0, NULL, NULL);
	if (bytes == 0) {
		free(normalized);
		return (NULL);
	}
	out = malloc((size_t)bytes);
	if (out == NULL) {
		free(normalized);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, normalized,
	    (int)j, out, bytes, NULL, NULL) == 0) {
		free(out);
		free(normalized);
		return (NULL);
	}
	free(normalized);
	*outlen = (size_t)bytes;
	return (out);
}

int
win32_clipboard_get_text(char **buf, size_t *len)
{
	HANDLE	 memory;
	wchar_t	*locked;
	char	*out;
	size_t	 outlen;
	int	 retval = -1;

	if (buf == NULL || len == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*buf = NULL;
	*len = 0;

	if (win32_clipboard_open() != 0)
		return (-1);
	memory = GetClipboardData(CF_UNICODETEXT);
	if (memory == NULL)
		goto out;
	locked = GlobalLock(memory);
	if (locked == NULL)
		goto out;
	out = win32_clipboard_text_to_utf8(locked, &outlen);
	GlobalUnlock(memory);
	if (out == NULL)
		goto out;

	*buf = out;
	*len = outlen;
	retval = 0;

out:
	CloseClipboard();
	return (retval);
}

#endif
