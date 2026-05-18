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

#ifndef TMUX_WIN32_SPAWN_H
#define TMUX_WIN32_SPAWN_H

#ifdef _WIN32

#include <stdint.h>

#include "win32-process.h"
#include "win32-pty.h"

struct win32_spawn_options {
	int			 argc;
	char *const		*argv;
	const char		*cwd;
	const char *const	*environment;
	int			 environment_count;
	unsigned short		 columns;
	unsigned short		 rows;
};

int	win32_spawn_pty(const struct win32_spawn_options *, struct win32_pty *,
	    uintptr_t *);
int	win32_spawn_process(const struct win32_spawn_options *,
	    struct win32_process *, uintptr_t *, int);

#endif

#endif
