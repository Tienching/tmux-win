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

#ifndef TMUX_WIN32_RAND_H
#define TMUX_WIN32_RAND_H

#ifdef _WIN32

#include <stddef.h>

/*
 * Fill `buf` with `len` cryptographically strong random bytes via
 * BCryptGenRandom(BCRYPT_USE_SYSTEM_PREFERRED_RNG). Returns 0 on success
 * and -1 on failure with last-error set; on failure `buf` is left
 * untouched.
 *
 * Used to generate the 16-byte handshake cookie (design sec 2.2). Linux
 * port of getentropy(3) for callers that need POSIX-style entropy without
 * the legacy SystemFunction036 indirection in win32_ipc_random.
 */
int	win32_rand_bytes(void *buf, size_t len);

#endif /* _WIN32 */

#endif /* TMUX_WIN32_RAND_H */
