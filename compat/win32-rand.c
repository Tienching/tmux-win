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

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <bcrypt.h>

#include "win32-rand.h"

/*
 * NTSTATUS success is exactly zero. We avoid pulling in <ntstatus.h>
 * (with the WIN32_NO_STATUS gymnastics) by checking against zero.
 */
#ifndef BCRYPT_SUCCESS
#define BCRYPT_SUCCESS(s) ((s) == 0)
#endif

int
win32_rand_bytes(void *buf, size_t len)
{
	NTSTATUS status;

	if (buf == NULL || len == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (len > (size_t)0xFFFFFFFFu) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}

	status = BCryptGenRandom(NULL, (PUCHAR)buf, (ULONG)len,
	    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
	if (!BCRYPT_SUCCESS(status)) {
		SetLastError(ERROR_GEN_FAILURE);
		return (-1);
	}
	return (0);
}

#endif /* _WIN32 */
