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

#ifndef TMUX_WIN32_ENDPOINT_H
#define TMUX_WIN32_ENDPOINT_H

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <stddef.h>
#include <wchar.h>

#define WIN32_ENDPOINT_VERSION		1
#define WIN32_ENDPOINT_SID_MAX		256
#define WIN32_ENDPOINT_PIPE_MAX		260
#define WIN32_ENDPOINT_TIME_MAX		32
#define WIN32_ENDPOINT_VERSION_STR_MAX	64

/*
 * Flat 7-field record persisted to %LOCALAPPDATA%\tmux\<sid>\<sock>.endpoint.
 * Hand-rolled swprintf_s/swscanf_s reader keeps total parser well under
 * 80 LOC and avoids a JSON dependency (PRD/design Q-1 + design sec 2.3).
 *
 * Layout (one field per line, version always first so v2 can detect):
 *   line 1: <version>
 *   line 2: <sid>
 *   line 3: <pid>
 *   line 4: <pipe_ctl>
 *   line 5: <pipe_evt>
 *   line 6: <started_at>      (ISO-8601 UTC, e.g. 2025-01-15T08:21:33Z)
 *   line 7: <tmux_version>    (e.g. 3.5a-win-port-rc1)
 */
struct win32_endpoint_record {
	int		version;
	wchar_t		sid[WIN32_ENDPOINT_SID_MAX];
	DWORD		pid;
	wchar_t		pipe_ctl[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t		pipe_evt[WIN32_ENDPOINT_PIPE_MAX];
	wchar_t		started_at[WIN32_ENDPOINT_TIME_MAX];
	wchar_t		tmux_version[WIN32_ENDPOINT_VERSION_STR_MAX];
};

/*
 * Atomic write: first write to <path>.tmp with FILE_FLAG_WRITE_THROUGH +
 * FlushFileBuffers, then MoveFileExW(MOVEFILE_REPLACE_EXISTING).
 * On power-cut the .tmp may be left behind, but the canonical file is
 * never observed at zero bytes.
 */
int	win32_endpoint_write_atomic(const wchar_t *path,
	    const struct win32_endpoint_record *record);
int	win32_endpoint_read(const wchar_t *path,
	    struct win32_endpoint_record *record);
int	win32_endpoint_unlink(const wchar_t *path);

/*
 * Helper: build absolute endpoint file path for the given socket name and
 * the current user SID. Caller frees with free(). socket_name is UTF-8.
 *
 * Layout: %LOCALAPPDATA%\tmux\<user-sid>\<socket-name>.endpoint
 */
int	win32_endpoint_resolve_path(const char *socket_name,
	    wchar_t **path_out);

/* Helper: derive ctl/evt named pipe paths matching the endpoint record. */
int	win32_endpoint_format_pipe_names(const char *socket_name,
	    wchar_t pipe_ctl[WIN32_ENDPOINT_PIPE_MAX],
	    wchar_t pipe_evt[WIN32_ENDPOINT_PIPE_MAX]);

#endif /* _WIN32 */

#endif /* TMUX_WIN32_ENDPOINT_H */
