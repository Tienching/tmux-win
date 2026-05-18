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

#ifndef TMUX_WIN32_HANDLE_H
#define TMUX_WIN32_HANDLE_H

#ifdef _WIN32

#include <stdint.h>

/* The source handle is a Windows console input or output handle. */
#define WIN32_HANDLE_MESSAGE_CONSOLE 0x1

struct win32_handle_message {
	uint32_t	process_id;
	uint32_t	flags;
	uint64_t	handle;
};

int	win32_handle_message_from_handle(void *, int,
	    struct win32_handle_message *);
int	win32_handle_message_from_fd(int, struct win32_handle_message *);
int	win32_handle_message_to_fd(const struct win32_handle_message *, int);

#endif

#endif
