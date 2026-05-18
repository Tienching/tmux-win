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

#include <errno.h>
#include <time.h>

#include "compat.h"

struct tm *
localtime_r(const time_t *timep, struct tm *result)
{
	if (timep == NULL || result == NULL) {
		errno = EINVAL;
		return (NULL);
	}
	if (localtime_s(result, timep) != 0)
		return (NULL);
	return (result);
}

struct tm *
gmtime_r(const time_t *timep, struct tm *result)
{
	if (timep == NULL || result == NULL) {
		errno = EINVAL;
		return (NULL);
	}
	if (gmtime_s(result, timep) != 0)
		return (NULL);
	return (result);
}

char *
ctime_r(const time_t *timep, char *buf)
{
	if (timep == NULL || buf == NULL) {
		errno = EINVAL;
		return (NULL);
	}
	if (ctime_s(buf, 26, timep) != 0)
		return (NULL);
	return (buf);
}

#endif /* _WIN32 */
