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

#include <algorithm>
#include <cstring>
#include <new>
#include <regex>
#include <string>

#include "win32-regex.h"

int
regcomp(regex_t *preg, const char *pattern, int cflags)
{
	std::regex::flag_type flags;

	if (preg == NULL || pattern == NULL)
		return (REG_BADPAT);

	flags = std::regex_constants::extended;
	if (cflags & REG_ICASE)
		flags |= std::regex_constants::icase;

	try {
		preg->opaque = new std::regex(pattern, flags);
		preg->cflags = cflags;
		return (0);
	} catch (const std::bad_alloc &) {
		preg->opaque = NULL;
		return (REG_ESPACE);
	} catch (const std::regex_error &) {
		preg->opaque = NULL;
		return (REG_BADPAT);
	}
}

int
regexec(const regex_t *preg, const char *string, size_t nmatch,
    regmatch_t pmatch[], int eflags)
{
	const std::regex		*regex;
	std::cmatch		 match;
	std::regex_constants::match_flag_type flags;
	size_t			 i, limit;

	if (preg == NULL || preg->opaque == NULL || string == NULL)
		return (REG_BADPAT);

	regex = static_cast<const std::regex *>(preg->opaque);
	flags = std::regex_constants::match_default;
	if (eflags & REG_NOTBOL)
		flags |= std::regex_constants::match_not_bol;

	try {
		if (!std::regex_search(string, match, *regex, flags))
			return (REG_NOMATCH);
	} catch (const std::regex_error &) {
		return (REG_BADPAT);
	}

	if ((preg->cflags & REG_NOSUB) || nmatch == 0 || pmatch == NULL)
		return (0);

	for (i = 0; i < nmatch; i++) {
		pmatch[i].rm_so = -1;
		pmatch[i].rm_eo = -1;
	}
	limit = std::min(nmatch, match.size());
	for (i = 0; i < limit; i++) {
		if (!match[i].matched)
			continue;
		pmatch[i].rm_so = (regoff_t)match.position(i);
		pmatch[i].rm_eo = (regoff_t)(match.position(i) +
		    match.length(i));
	}
	return (0);
}

size_t
regerror(int error, const regex_t *preg, char *buffer, size_t size)
{
	const char	*message;
	size_t		 needed;

	(void)preg;

	switch (error) {
	case 0:
		message = "no error";
		break;
	case REG_NOMATCH:
		message = "no match";
		break;
	case REG_ESPACE:
		message = "out of memory";
		break;
	default:
		message = "invalid regular expression";
		break;
	}

	needed = strlen(message) + 1;
	if (buffer != NULL && size != 0) {
		strncpy(buffer, message, size - 1);
		buffer[size - 1] = '\0';
	}
	return (needed);
}

void
regfree(regex_t *preg)
{
	if (preg == NULL)
		return;
	delete static_cast<std::regex *>(preg->opaque);
	preg->opaque = NULL;
	preg->cflags = 0;
}

#endif
