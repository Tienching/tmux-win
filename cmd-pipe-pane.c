/* $OpenBSD$ */

/*
 * Copyright (c) 2009 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif

#include <errno.h>
#include <fcntl.h>
#ifndef _WIN32
#include <signal.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "tmux.h"

#ifdef _WIN32
#include "compat/win32-command.h"
#include "compat/win32-process.h"
#include "compat/win32-socketpair.h"
#include "compat/win32-spawn.h"
#endif

/*
 * Open pipe to redirect pane output. If already open, close first.
 */

static enum cmd_retval	cmd_pipe_pane_exec(struct cmd *, struct cmdq_item *);

static void cmd_pipe_pane_close(struct window_pane *);
#ifdef _WIN32
static const char *cmd_pipe_pane_win32_shell(struct session *);
#endif
static void cmd_pipe_pane_read_callback(struct bufferevent *, void *);
static void cmd_pipe_pane_write_callback(struct bufferevent *, void *);
static void cmd_pipe_pane_error_callback(struct bufferevent *, short, void *);

const struct cmd_entry cmd_pipe_pane_entry = {
	.name = "pipe-pane",
	.alias = "pipep",

	.args = { "IOot:", 0, 1, NULL },
	.usage = "[-IOo] " CMD_TARGET_PANE_USAGE " [shell-command]",

	.target = { 't', CMD_FIND_PANE, 0 },

	.flags = CMD_AFTERHOOK,
	.exec = cmd_pipe_pane_exec
};

static void
cmd_pipe_pane_close(struct window_pane *wp)
{
	if (wp->pipe_fd == -1)
		return;

	bufferevent_free(wp->pipe_event);
#ifdef _WIN32
	win32_socket_close(wp->win32_pipe_socket);
	if (wp->win32_pipe_process != NULL) {
		win32_process_close((struct win32_process *)
		    wp->win32_pipe_process);
		free(wp->win32_pipe_process);
		wp->win32_pipe_process = NULL;
	}
	wp->win32_pipe_socket = (uintptr_t)-1;
#else
	close(wp->pipe_fd);
#endif
	wp->pipe_fd = -1;
}

#ifdef _WIN32
static const char *
cmd_pipe_pane_win32_fallback_shell(void)
{
	const char	*shell;

	shell = getenv("ComSpec");
	if (shell != NULL && checkshell(shell))
		return (shell);
	shell = getenv("SHELL");
	if (shell != NULL && checkshell(shell))
		return (shell);
	return (get_default_shell());
}

static const char *
cmd_pipe_pane_win32_shell(struct session *s)
{
	const char	*shell;

	if (s != NULL)
		shell = options_get_string(s->options, "default-shell");
	else
		shell = options_get_string(global_s_options, "default-shell");
	if (checkshell(shell))
		return (shell);
	return (cmd_pipe_pane_win32_fallback_shell());
}

#endif

static enum cmd_retval
cmd_pipe_pane_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = cmd_get_args(self);
	struct cmd_find_state		*target = cmdq_get_target(item);
	struct client			*tc = cmdq_get_target_client(item);
	struct window_pane		*wp = target->wp;
	struct session			*s = target->s;
	struct winlink			*wl = target->wl;
	struct window_pane_offset	*wpo = &wp->pipe_offset;
	char				*cmd;
	int				 old_fd, in, out;
#ifndef _WIN32
	int				 pipe_fd[2], null_fd;
#endif
	struct format_tree		*ft;
#ifndef _WIN32
	sigset_t			 set, oldset;
#else
	struct win32_spawn_options	 options;
	struct win32_process		*process;
	uintptr_t			 pipe_socket;
	char				*argv[4];
	const char			*shell, *cwd;
	int				 spawn_argc;
#endif

	/* Do nothing if pane is dead. */
	if (window_pane_exited(wp)) {
		cmdq_error(item, "target pane has exited");
		return (CMD_RETURN_ERROR);
	}

	/* Destroy the old pipe. */
	old_fd = wp->pipe_fd;
	if (wp->pipe_fd != -1) {
		cmd_pipe_pane_close(wp);
		if (window_pane_destroy_ready(wp)) {
			server_destroy_pane(wp, 1);
			return (CMD_RETURN_NORMAL);
		}
	}

	/* If no pipe command, that is enough. */
	if (args_count(args) == 0 || *args_string(args, 0) == '\0')
		return (CMD_RETURN_NORMAL);

	/*
	 * With -o, only open the new pipe if there was no previous one. This
	 * allows a pipe to be toggled with a single key, for example:
	 *
	 *	bind ^p pipep -o 'cat >>~/output'
	 */
	if (args_has(args, 'o') && old_fd != -1)
		return (CMD_RETURN_NORMAL);

	/* What do we want to do? Neither -I or -O is -O. */
	if (args_has(args, 'I')) {
		in = 1;
		out = args_has(args, 'O');
	} else {
		in = 0;
		out = 1;
	}

#ifndef _WIN32
	/* Open the new pipe. */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pipe_fd) != 0) {
		cmdq_error(item, "socketpair error: %s", strerror(errno));
		return (CMD_RETURN_ERROR);
	}
#endif

	/* Expand the command. */
	ft = format_create(cmdq_get_client(item), item, FORMAT_NONE, 0);
	format_defaults(ft, tc, s, wl, wp);
	cmd = format_expand_time(ft, args_string(args, 0));
	format_free(ft);

#ifdef _WIN32
	shell = cmd_pipe_pane_win32_shell(s);
	spawn_argc = win32_shell_command_argv(shell, cmd, argv);
	cwd = server_client_get_cwd(tc, s);
	if (!path_is_directory(cwd))
		cwd = find_default_cwd();

	process = xcalloc(1, sizeof *process);
	memset(&options, 0, sizeof options);
	options.argc = spawn_argc;
	options.argv = argv;
	options.cwd = cwd;

	if (win32_spawn_process(&options, process, &pipe_socket, 0) != 0) {
		cmdq_error(item, "spawn error: Windows error %lu",
		    GetLastError());
		free(process);
		free(cmd);
		return (CMD_RETURN_ERROR);
	}
	if (win32_socket_set_blocking(pipe_socket, 0) != 0) {
		cmdq_error(item, "socket error: Windows error %lu",
		    GetLastError());
		win32_process_close(process);
		free(process);
		win32_socket_close(pipe_socket);
		free(cmd);
		return (CMD_RETURN_ERROR);
	}

	wp->pipe_fd = 0;
	wp->pipe_pid = (pid_t)win32_process_id(process);
	wp->win32_pipe_socket = pipe_socket;
	wp->win32_pipe_process = process;
	memcpy(wpo, &wp->offset, sizeof *wpo);

	wp->pipe_event = bufferevent_new((evutil_socket_t)pipe_socket,
	    cmd_pipe_pane_read_callback, cmd_pipe_pane_write_callback,
	    cmd_pipe_pane_error_callback, wp);
	if (wp->pipe_event == NULL)
		fatalx("out of memory");
	if (out)
		bufferevent_enable(wp->pipe_event, EV_WRITE);
	if (in)
		bufferevent_enable(wp->pipe_event, EV_READ);

	free(cmd);
	return (CMD_RETURN_NORMAL);
#else
	/* Fork the child. */
	sigfillset(&set);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	switch ((wp->pipe_pid = fork())) {
	case -1:
		sigprocmask(SIG_SETMASK, &oldset, NULL);
		cmdq_error(item, "fork error: %s", strerror(errno));

		close(pipe_fd[0]);
		close(pipe_fd[1]);
		free(cmd);
		return (CMD_RETURN_ERROR);
	case 0:
		/* Child process. */
		proc_clear_signals(server_proc, 1);
		sigprocmask(SIG_SETMASK, &oldset, NULL);
		close(pipe_fd[0]);

		if (setpgid(0, 0) == -1)
			_exit(1);

		null_fd = open(_PATH_DEVNULL, O_WRONLY);
		if (out) {
			if (dup2(pipe_fd[1], STDIN_FILENO) == -1)
				_exit(1);
		} else {
			if (dup2(null_fd, STDIN_FILENO) == -1)
				_exit(1);
		}
		if (in) {
			if (dup2(pipe_fd[1], STDOUT_FILENO) == -1)
				_exit(1);
			if (pipe_fd[1] != STDOUT_FILENO)
				close(pipe_fd[1]);
		} else {
			if (dup2(null_fd, STDOUT_FILENO) == -1)
				_exit(1);
		}
		if (dup2(null_fd, STDERR_FILENO) == -1)
			_exit(1);
		closefrom(STDERR_FILENO + 1);

		execl(_PATH_BSHELL, "sh", "-c", cmd, (char *) NULL);
		_exit(1);
	default:
		/* Parent process. */
		sigprocmask(SIG_SETMASK, &oldset, NULL);
		close(pipe_fd[1]);

		wp->pipe_fd = pipe_fd[0];
		memcpy(wpo, &wp->offset, sizeof *wpo);

		setblocking(wp->pipe_fd, 0);
		wp->pipe_event = bufferevent_new(wp->pipe_fd,
		    cmd_pipe_pane_read_callback,
		    cmd_pipe_pane_write_callback,
		    cmd_pipe_pane_error_callback,
		    wp);
		if (wp->pipe_event == NULL)
			fatalx("out of memory");
		if (out)
			bufferevent_enable(wp->pipe_event, EV_WRITE);
		if (in)
			bufferevent_enable(wp->pipe_event, EV_READ);

		free(cmd);
		return (CMD_RETURN_NORMAL);
	}
#endif
}

static void
cmd_pipe_pane_read_callback(__unused struct bufferevent *bufev, void *data)
{
	struct window_pane	*wp = data;
	struct evbuffer		*evb = wp->pipe_event->input;
	size_t			 available;

	available = EVBUFFER_LENGTH(evb);
	log_debug("%%%u pipe read %zu", wp->id, available);

	bufferevent_write(wp->event, EVBUFFER_DATA(evb), available);
	evbuffer_drain(evb, available);

	if (window_pane_destroy_ready(wp))
		server_destroy_pane(wp, 1);
}

static void
cmd_pipe_pane_write_callback(__unused struct bufferevent *bufev, void *data)
{
	struct window_pane	*wp = data;

	log_debug("%%%u pipe empty", wp->id);

	if (window_pane_destroy_ready(wp))
		server_destroy_pane(wp, 1);
}

static void
cmd_pipe_pane_error_callback(__unused struct bufferevent *bufev,
    __unused short what, void *data)
{
	struct window_pane	*wp = data;

	log_debug("%%%u pipe error", wp->id);

	cmd_pipe_pane_close(wp);

	if (window_pane_destroy_ready(wp))
		server_destroy_pane(wp, 1);
}
