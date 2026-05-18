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

#ifndef TMUX_WIN32_PROCESS_H
#define TMUX_WIN32_PROCESS_H

#ifdef _WIN32

#include <stdint.h>
#include <wchar.h>

struct win32_process_options {
	const wchar_t	*command;
	const wchar_t	*cwd;
	const wchar_t	*environment;
	int		 show_stderr;
};

struct win32_process {
	void		*input;
	void		*output;
	void		*process;
	void		*thread;
	void		*job;
	uintptr_t	 bridge_socket;
	void		*input_thread;
	void		*output_thread;
	unsigned long	 process_id;
};

int		win32_process_spawn(struct win32_process *,
		    const struct win32_process_options *, uintptr_t *);
int		win32_process_exited(const struct win32_process *,
		    unsigned long *);
int		win32_process_wait(struct win32_process *, unsigned long,
		    unsigned long *);
int		win32_process_terminate(struct win32_process *, unsigned int);
void		win32_process_close(struct win32_process *);
unsigned long	win32_process_id(const struct win32_process *);

#endif

#endif
