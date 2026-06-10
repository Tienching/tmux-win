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
#include "win32-environment.h"

static int
win32_environment_cmp(const void *a0, const void *b0)
{
	const wchar_t *const	*a = a0;
	const wchar_t *const	*b = b0;
	int			 r;

	/*
	 * Use CompareStringOrdinal with NORM_IGNORECASE rather than
	 * _wcsicmp(): _wcsicmp is locale-aware (LC_CTYPE) and produces
	 * surprising results in locales such as Turkish where 'i'/'I' do
	 * not pair with 'I'/'i'. Windows' CreateProcessW expects environment
	 * blocks to be sorted in a culture-invariant, case-insensitive order
	 * regardless of the user's locale.
	 *
	 * CompareStringOrdinal returns CSTR_{LESS,EQUAL,GREATER}_THAN; map
	 * those to the qsort()-style -1/0/+1 contract.
	 */
	r = CompareStringOrdinal(*a, -1, *b, -1, TRUE);
	switch (r) {
	case CSTR_LESS_THAN:
		return (-1);
	case CSTR_GREATER_THAN:
		return (1);
	case CSTR_EQUAL:
		return (0);
	default:
		/* Fall back if the API rejects one of the inputs. */
		return (_wcsicmp(*a, *b));
	}
}

wchar_t *
win32_build_environment_block(int count, const char *const *vars)
{
	wchar_t	**wide_vars, *block, *out;
	size_t	  total, len;
	int	  i;

	if (count < 0 || (count != 0 && vars == NULL)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (NULL);
	}

	/*
	 * count==0 means inherit the parent environment.  Return NULL so
	 * that CreateProcessW uses the current process's environment block.
	 * A non-NULL empty block would need to be double-NUL terminated,
	 * which would give the child an empty environment instead of
	 * inheriting.
	 */
	if (count == 0)
		return (NULL);

	total = 1; /* trailing NUL terminator */
	wide_vars = calloc(count, sizeof *wide_vars);
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
