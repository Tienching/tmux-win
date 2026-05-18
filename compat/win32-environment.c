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

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "win32-command.h"
#include "win32-environment.h"

static int
win32_environment_cmp(const void *a0, const void *b0)
{
	const wchar_t *const	*a = a0;
	const wchar_t *const	*b = b0;

	return (_wcsicmp(*a, *b));
}

wchar_t *
win32_build_environment_block(int count, const char *const *vars)
{
	wchar_t	**wide_vars, *block, *out;
	size_t	  total = 1, len;
	int	  i;

	if (count < 0 || (count != 0 && vars == NULL)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	wide_vars = calloc(count == 0 ? 1 : count, sizeof *wide_vars);
	if (wide_vars == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}

	for (i = 0; i < count; i++) {
		if (vars[i] == NULL || vars[i][0] == '\0' ||
		    strchr(vars[i], '=') == NULL) {
			SetLastError(ERROR_INVALID_PARAMETER);
			goto fail;
		}
		wide_vars[i] = win32_utf8_to_wide(vars[i]);
		if (wide_vars[i] == NULL)
			goto fail;
		len = wcslen(wide_vars[i]);
		if (len > SIZE_MAX - total - 1) {
			SetLastError(ERROR_NOT_ENOUGH_MEMORY);
			goto fail;
		}
		total += len + 1;
	}
	qsort(wide_vars, count, sizeof *wide_vars, win32_environment_cmp);

	block = calloc(total, sizeof *block);
	if (block == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto fail;
	}
	out = block;
	for (i = 0; i < count; i++) {
		len = wcslen(wide_vars[i]) + 1;
		memcpy(out, wide_vars[i], len * sizeof *out);
		out += len;
	}

	for (i = 0; i < count; i++)
		free(wide_vars[i]);
	free(wide_vars);
	return (block);

fail:
	for (i = 0; i < count; i++)
		free(wide_vars[i]);
	free(wide_vars);
	return (NULL);
}

#endif
