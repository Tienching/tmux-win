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

#ifndef TMUX_WIN32_STDIO_H
#define TMUX_WIN32_STDIO_H

#ifdef _WIN32

#include <stddef.h>
#include <stdint.h>

/*
 * Shared cursor state between the main thread and the stdio output worker.
 * The bridge holds a pointer; the output worker holds its own pointer.
 * The bridge close path waits for the worker to exit before freeing this.
 */
struct win32_stdio_cursor {
	CRITICAL_SECTION	 lock;
	short			 pending_x;
	short			 pending_y;
	int			 pending_valid;
	HANDLE			 console_handle;
};

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
	unsigned int	output_codepage_valid;
	unsigned int	output_codepage;
	int		input_mode_valid;
	int		output_mode_valid;
	int		input_codepage_valid;
	int		input_console;
	int		output_console;
	struct win32_stdio_cursor	*cursor;
};

int	win32_stdio_bridge_open(struct win32_stdio_bridge *, int, int,
	    uintptr_t *, uintptr_t *, int);
int	win32_stdio_bridge_get_size(struct win32_stdio_bridge *, unsigned int *,
	    unsigned int *);
int	win32_stdio_bridge_prepare_terminal(struct win32_stdio_bridge *);
int	win32_stdio_bridge_feed_input(struct win32_stdio_bridge *, const void *,
	    size_t);
void	win32_stdio_bridge_restore_terminal(struct win32_stdio_bridge *);
void	win32_stdio_bridge_sync_console_cursor(struct win32_stdio_bridge *, unsigned int,
	    unsigned int);
int	win32_stdio_bridge_close(struct win32_stdio_bridge *);

#endif

#endif
