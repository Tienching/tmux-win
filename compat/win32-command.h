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

#ifndef TMUX_WIN32_COMMAND_H
#define TMUX_WIN32_COMMAND_H

#ifdef _WIN32

#include <wchar.h>

wchar_t	*win32_utf8_to_wide(const char *);
wchar_t	*win32_utf8_to_wide_path(const char *);
char	*win32_wide_to_utf8(const wchar_t *);
char	*win32_wide_to_utf8_path(const wchar_t *);
wchar_t	*win32_build_command_line(int, char *const *);
wchar_t	*win32_build_command_line_wide(int, const wchar_t *const *);
int	 win32_shell_is_cmd(const char *);
int	 win32_shell_command_argv(const char *, const char *, char **);

#endif

#endif
