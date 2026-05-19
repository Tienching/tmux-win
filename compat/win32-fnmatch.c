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

#include <ctype.h>
#include <string.h>

#ifndef FNM_NOMATCH
#define FNM_NOMATCH 1
#endif
#ifndef FNM_NOESCAPE
#define FNM_NOESCAPE 0x01
#endif
#ifndef FNM_PATHNAME
#define FNM_PATHNAME 0x02
#endif
#ifndef FNM_PERIOD
#define FNM_PERIOD 0x04
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x08
#endif

static int
win32_fnmatch_fold(int ch, int flags)
{
	if (flags & FNM_CASEFOLD)
		return (tolower((unsigned char)ch));
	return ((unsigned char)ch);
}

static int
win32_fnmatch_path_separator(int ch)
{
	return (ch == '/' || ch == '\\');
}

static int
win32_fnmatch_range(const char **pattern, int ch, int flags)
{
	const char	*next = *pattern;
	int		 negate = 0, matched = 0, first, last;

	if (*next == '!' || *next == '^') {
		negate = 1;
		next++;
	}
	if (*next == ']') {
		if (win32_fnmatch_fold(*next, flags) == ch)
			matched = 1;
		next++;
	}

	while (*next != '\0' && *next != ']') {
		if (*next == '\\' && !(flags & FNM_NOESCAPE) &&
		    next[1] != '\0')
			next++;
		first = win32_fnmatch_fold(*next++, flags);
		if (*next == '-' && next[1] != '\0' && next[1] != ']') {
			next++;
			if (*next == '\\' && !(flags & FNM_NOESCAPE) &&
			    next[1] != '\0')
				next++;
			last = win32_fnmatch_fold(*next++, flags);
			if (first <= ch && ch <= last)
				matched = 1;
		} else if (first == ch)
			matched = 1;
	}
	if (*next != ']')
		return (-1);
	*pattern = next + 1;
	return (matched != negate);
}

static int
win32_fnmatch(const char *pattern, const char *string, int flags,
    int segment_start)
{
	const char	*retry_pattern = NULL, *retry_string = NULL;
	int		 ch, matched;

	for (;;) {
		switch (*pattern) {
		case '\0':
			if (*string == '\0')
				return (0);
			break;
		case '?':
			if (*string == '\0')
				break;
			if ((flags & FNM_PATHNAME) &&
			    win32_fnmatch_path_separator((unsigned char)*string))
				break;
			if ((flags & FNM_PERIOD) && segment_start &&
			    *string == '.')
				break;
			pattern++;
			segment_start = win32_fnmatch_path_separator(
			    (unsigned char)*string);
			string++;
			continue;
		case '*':
			while (*pattern == '*')
				pattern++;
			if ((flags & FNM_PERIOD) && segment_start &&
			    *string == '.')
				break;
			if (*pattern == '\0') {
				if ((flags & FNM_PATHNAME) &&
				    strpbrk(string, "/\\") != NULL)
					break;
				return (0);
			}
			retry_pattern = pattern;
			retry_string = string;
			continue;
		case '[':
			if (*string == '\0')
				break;
			if ((flags & FNM_PATHNAME) &&
			    win32_fnmatch_path_separator((unsigned char)*string))
				break;
			if ((flags & FNM_PERIOD) && segment_start &&
			    *string == '.')
				break;
			pattern++;
			ch = win32_fnmatch_fold((unsigned char)*string, flags);
			matched = win32_fnmatch_range(&pattern, ch, flags);
			if (matched != 1)
				break;
			segment_start = win32_fnmatch_path_separator(
			    (unsigned char)*string);
			string++;
			continue;
		case '\\':
			if (!(flags & FNM_NOESCAPE) && pattern[1] != '\0')
				pattern++;
			/* FALLTHROUGH */
		default:
			if (win32_fnmatch_fold((unsigned char)*pattern, flags) !=
			    win32_fnmatch_fold((unsigned char)*string, flags))
				break;
			segment_start = win32_fnmatch_path_separator(
			    (unsigned char)*string);
			pattern++;
			string++;
			continue;
		}

		if (retry_pattern == NULL || *retry_string == '\0')
			return (FNM_NOMATCH);
		if ((flags & FNM_PATHNAME) &&
		    win32_fnmatch_path_separator((unsigned char)*retry_string))
			return (FNM_NOMATCH);
		segment_start = win32_fnmatch_path_separator(
		    (unsigned char)*retry_string);
		string = ++retry_string;
		pattern = retry_pattern;
	}
}

int
fnmatch(const char *pattern, const char *string, int flags)
{
	if (pattern == NULL || string == NULL)
		return (FNM_NOMATCH);
	return (win32_fnmatch(pattern, string, flags, 1));
}

#endif
