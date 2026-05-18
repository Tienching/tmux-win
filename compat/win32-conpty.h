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

#ifndef TMUX_WIN32_CONPTY_H
#define TMUX_WIN32_CONPTY_H

#ifdef _WIN32

#include <wchar.h>

struct win32_conpty {
	void		*pseudoconsole;
	void		*input;
	void		*output;
	void		*process;
	void		*thread;
	void		*job;
	unsigned long	 process_id;
};

int	win32_conpty_available(void);
int	win32_conpty_spawn(struct win32_conpty *, const wchar_t *,
	    const wchar_t *, const wchar_t *, unsigned short, unsigned short);
int	win32_conpty_resize(struct win32_conpty *, unsigned short,
	    unsigned short);
void	win32_conpty_close(struct win32_conpty *);

#endif

#endif
