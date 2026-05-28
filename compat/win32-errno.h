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

#ifndef TMUX_WIN32_ERRNO_H
#define TMUX_WIN32_ERRNO_H

#ifdef _WIN32

/*
 * Translate the most recent Windows error (GetLastError()) into the
 * closest POSIX errno value. Used at every win32 -> POSIX boundary so
 * upper layers can keep relying on errno-style branches without hard-
 * coding Win32 codes (design sec 5).
 *
 * Coverage: >= 20 mappings (file/path, access, IPC, sync, memory,
 * networking-adjacent ENXIO/EPIPE). Unknown codes collapse to EINVAL,
 * matching common practice in cygwin's __set_winsock_errno.
 */
int	win32_errno_from_lasterror(void);
int	win32_errno_from_code(unsigned long code);

#endif /* _WIN32 */

#endif /* TMUX_WIN32_ERRNO_H */
