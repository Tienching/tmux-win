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

#ifndef TMUX_WIN32_STDIO_H
#define TMUX_WIN32_STDIO_H

#ifdef _WIN32

#include <stddef.h>
#include <stdint.h>

struct win32_stdio_bridge {
	int		input_fd;
	int		output_fd;
	uintptr_t	input_socket;
	uintptr_t	output_socket;
	uintptr_t	input_bridge_socket;
	uintptr_t	output_bridge_socket;
	void		*input_thread;
	void		*output_thread;
	unsigned long	input_mode;
	unsigned long	output_mode;
	unsigned int	input_codepage;
	unsigned int	output_codepage;
	int		input_mode_valid;
	int		output_mode_valid;
	int		input_codepage_valid;
	int		output_codepage_valid;
	int		input_console;
};

int	win32_stdio_bridge_open(struct win32_stdio_bridge *, int, int,
	    uintptr_t *, uintptr_t *, int);
int	win32_stdio_bridge_get_size(struct win32_stdio_bridge *, unsigned int *,
	    unsigned int *);
int	win32_stdio_bridge_prepare_terminal(struct win32_stdio_bridge *);
int	win32_stdio_bridge_feed_input(struct win32_stdio_bridge *, const void *,
	    size_t);
void	win32_stdio_bridge_restore_terminal(struct win32_stdio_bridge *);
void	win32_stdio_bridge_close(struct win32_stdio_bridge *);

#endif

#endif
