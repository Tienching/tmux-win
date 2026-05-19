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

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "win32-command.h"
#include "win32-environment.h"
#include "win32-process.h"
#include "win32-spawn.h"

/*
 * Tiny CWD string helpers. These were previously duplicated as
 * `job_win32_*` and `spawn_win32_*` static helpers in job.c and
 * spawn.c; they live here so the UNC / long-path / pushd policy stays
 * in one place.
 *
 * NOTE: These helpers intentionally use plain C runtime calls
 * (no xasprintf) so they remain usable from the compat layer without
 * pulling in tmux.h.
 */

int
win32_spawn_cwd_is_unc(const char *cwd)
{
	return (cwd != NULL && cwd[0] == '\\' && cwd[1] == '\\' &&
	    strncmp(cwd, "\\\\?\\", 4) != 0);
}

int
win32_spawn_cwd_is_process_supported(const char *cwd)
{
	if (cwd == NULL)
		return (0);
	if (isalpha((unsigned char)cwd[0]) && cwd[1] == ':' &&
	    (cwd[2] == '\\' || cwd[2] == '/') && strlen(cwd) >= MAX_PATH)
		return (0);
	return (1);
}

char *
win32_spawn_cmd_pushd(const char *cwd, const char *cmd)
{
	char	*new_cmd;
	int	 needed;

	if (cwd == NULL)
		return (NULL);
	if (cmd == NULL)
		needed = _scprintf("pushd \"%s\"", cwd);
	else
		needed = _scprintf("pushd \"%s\" && %s", cwd, cmd);
	if (needed < 0)
		abort();
	new_cmd = malloc((size_t)needed + 1);
	if (new_cmd == NULL)
		abort();
	if (cmd == NULL)
		snprintf(new_cmd, (size_t)needed + 1, "pushd \"%s\"", cwd);
	else
		snprintf(new_cmd, (size_t)needed + 1,
		    "pushd \"%s\" && %s", cwd, cmd);
	return (new_cmd);
}

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
	process_options.discard_stdout = options->discard_stdout;

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
