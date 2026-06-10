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
	int		 discard_stdout;
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
int		win32_process_close(struct win32_process *);
unsigned long	win32_process_id(const struct win32_process *);

/*
 * Translate a Windows GetExitCodeProcess() value into a POSIX-style status
 * word that the existing WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG macros
 * (defined in compat.h) decode correctly.
 *
 * NTSTATUS-shaped exception codes (0xC0000005 access violation, 0xC000013A
 * Ctrl+C, 0x40010005 DBG_CONTROL_C, ...) are mapped to (signal << 0) so
 * WIFSIGNALED is true and WTERMSIG returns SIGSEGV/SIGINT/etc. Plain
 * 0..255 exits are encoded as (code << 8) so WIFEXITED is true and
 * WEXITSTATUS returns the process's exit value.
 */
int		win32_native_exit_to_status(unsigned long native);

#endif
#endif
