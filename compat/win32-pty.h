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

#ifndef TMUX_WIN32_PTY_H
#define TMUX_WIN32_PTY_H

#ifdef _WIN32

#include <stdint.h>
#include <wchar.h>

#include "win32-conpty.h"

struct win32_pty_options {
	const wchar_t	*command;
	const wchar_t	*cwd;
	const wchar_t	*environment;
	unsigned short	 columns;
	unsigned short	 rows;
};

struct win32_pty {
	struct win32_conpty	 conpty;
	uintptr_t		 bridge_socket;
	void			*input_thread;
	void			*output_thread;
};

int		win32_pty_spawn(struct win32_pty *,
		    const struct win32_pty_options *, uintptr_t *);
int		win32_pty_resize(struct win32_pty *, unsigned short,
		    unsigned short);
int		win32_pty_exited(const struct win32_pty *, unsigned long *);
int		win32_pty_wait(struct win32_pty *, unsigned long,
		    unsigned long *);
int		win32_pty_send_ctrl_break(struct win32_pty *);
int		win32_pty_send_ctrl_break_to_pid(unsigned long);
int		win32_pty_terminate(struct win32_pty *, unsigned int);
void		win32_pty_close(struct win32_pty *);
unsigned long	win32_pty_process_id(const struct win32_pty *);

#endif

#endif
