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

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "win32-command.h"
#include "win32-environment.h"
#include "win32-process.h"
#include "win32-spawn.h"

int
win32_spawn_pty(const struct win32_spawn_options *options,
    struct win32_pty *pty, uintptr_t *master_socket)
{
	struct win32_pty_options	 pty_options;
	wchar_t			*command = NULL, *cwd = NULL, *environment = NULL;
	int			 retval;

	if (options == NULL || pty == NULL || master_socket == NULL ||
	    options->argc <= 0 || options->argv == NULL ||
	    options->environment_count < 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	command = win32_build_command_line(options->argc, options->argv);
	if (command == NULL)
		goto fail;
	if (options->cwd != NULL) {
		cwd = win32_utf8_to_wide_path(options->cwd);
		if (cwd == NULL)
			goto fail;
	}
	if (options->environment != NULL) {
		environment = win32_build_environment_block(
		    options->environment_count, options->environment);
		if (environment == NULL)
			goto fail;
	}

	memset(&pty_options, 0, sizeof pty_options);
	pty_options.command = command;
	pty_options.cwd = cwd;
	pty_options.environment = environment;
	pty_options.columns = options->columns;
	pty_options.rows = options->rows;

	retval = win32_pty_spawn(pty, &pty_options, master_socket);
	free(command);
	free(cwd);
	free(environment);
	return (retval);

fail:
	free(command);
	free(cwd);
	free(environment);
	return (-1);
}

int
win32_spawn_process(const struct win32_spawn_options *options,
    struct win32_process *process, uintptr_t *master_socket, int show_stderr)
{
	struct win32_process_options process_options;
	wchar_t			   *command = NULL, *cwd = NULL;
	wchar_t			   *environment = NULL;
	int			    retval;

	if (options == NULL || process == NULL || master_socket == NULL ||
	    options->argc <= 0 || options->argv == NULL ||
	    options->environment_count < 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	command = win32_build_command_line(options->argc, options->argv);
	if (command == NULL)
		goto fail;
	if (options->cwd != NULL) {
		cwd = win32_utf8_to_wide_path(options->cwd);
		if (cwd == NULL)
			goto fail;
	}
	if (options->environment != NULL) {
		environment = win32_build_environment_block(
		    options->environment_count, options->environment);
		if (environment == NULL)
			goto fail;
	}

	memset(&process_options, 0, sizeof process_options);
	process_options.command = command;
	process_options.cwd = cwd;
	process_options.environment = environment;
	process_options.show_stderr = show_stderr;

	retval = win32_process_spawn(process, &process_options, master_socket);
	free(command);
	free(cwd);
	free(environment);
	return (retval);

fail:
	free(command);
	free(cwd);
	free(environment);
	return (-1);
}

#endif
