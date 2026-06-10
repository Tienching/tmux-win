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
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#endif

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

#ifdef _WIN32
#include "compat/win32-command.h"
#include "compat/win32-socketpair.h"
#include "compat/win32-spawn.h"

#define WIN32_JOB_EXIT_GRACE_USEC 100000
#define WIN32_POLL_INTERVAL_MS 10
#define WIN32_POLL_IDLE_THRESHOLD 100
#define WIN32_POLL_IDLE_INTERVAL_MS 100
#endif

/*
 * Job scheduling. Run queued commands in the background and record their
 * output.
 */

#ifdef _WIN32
static struct job *job_run_win32_pty(const char *, const char *, int, char **,
	    struct environ *, const char *, job_update_cb, job_complete_cb,
	    job_free_cb, void *, int, int, int);
static struct job *job_run_win32_process(const char *, const char *, int, char **,
	    struct environ *, const char *, job_update_cb, job_complete_cb,
	    job_free_cb, void *, int);
static char	*job_win32_resolve_cwd(struct environ *, const char *);
static int	job_win32_collect_status(struct job *);
static void	job_win32_poll_callback(evutil_socket_t, short, void *);
static int	job_win32_status(unsigned long);
#endif

static void	job_read_callback(struct bufferevent *, void *);
static void	job_write_callback(struct bufferevent *, void *);
static void	job_error_callback(struct bufferevent *, short, void *);

/* A single job. */
struct job {
	enum {
		JOB_RUNNING,
		JOB_DEAD,
		JOB_CLOSED
	} state;

	int			 flags;

	char			*cmd;
	pid_t			 pid;
	char			 tty[TTY_NAME_MAX];
	int			 status;

	int			 fd;
	struct bufferevent	*event;

#ifdef _WIN32
	uintptr_t		 win32_socket;
	struct win32_pty	*win32_pty;
	struct win32_process	*win32_process;
	struct event		 win32_poll_event;
	struct timeval		 win32_exit_time;
	int			 win32_poll_idle_count;
#endif

	job_update_cb		 updatecb;
	job_complete_cb		 completecb;
	job_free_cb		 freecb;
	void			*data;

	LIST_ENTRY(job)		 entry;
};

/* All jobs list. */
static LIST_HEAD(joblist, job) all_jobs = LIST_HEAD_INITIALIZER(all_jobs);

#ifdef _WIN32
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

static int
job_win32_status(unsigned long exit_code)
{
	/*
	 * Encode the Win32 ExitCode the same way POSIX waitpid() encodes a
	 * status word so the existing WIFEXITED/WIFSIGNALED/WTERMSIG macros
	 * in compat.h decode correctly. Common NTSTATUS exception codes
	 * (0xC0000005, Ctrl-C, ...) are mapped to a POSIX signal in
	 * win32_native_exit_to_status() rather than being silently passed
	 * through where their low bits would collide with the signal field.
	 */
	return (win32_native_exit_to_status(exit_code));
}

static int
job_win32_collect_status(struct job *job)
{
	unsigned long	exit_code;
	int		exited = 0;

	if (job->win32_pty != NULL)
		exited = win32_pty_wait(job->win32_pty, 0, &exit_code);
	else if (job->win32_process != NULL)
		exited = win32_process_wait(job->win32_process, 0, &exit_code);
	if (exited != 1)
		return (0);

	job->status = job_win32_status(exit_code);
	job->pid = -1;
	job->state = JOB_DEAD;
	return (1);
}

static void
job_win32_poll_callback(__unused evutil_socket_t fd, __unused short events, void *data)
{
	struct job	*job = data;
	struct timeval	 tv;
	struct timeval	 now, diff;
	unsigned long	 pending;
	int		 was_closed, has_data = 0;

	if ((job->state != JOB_RUNNING && job->state != JOB_CLOSED &&
	    job->state != JOB_DEAD) || job->event == NULL)
		return;

	if (job->win32_socket != (uintptr_t)-1 &&
	    win32_socket_pending(job->win32_socket, &pending) == 0 &&
	    pending != 0) {
		has_data = 1;
		event_active(&job->event->ev_read, EV_READ, 1);
		tv.tv_sec = 0;
		tv.tv_usec = WIN32_POLL_INTERVAL_MS * 1000;
		job->win32_poll_idle_count = 0;
		evtimer_add(&job->win32_poll_event, &tv);
		return;
	}

	was_closed = (job->state == JOB_CLOSED);
	if (job->state == JOB_RUNNING && job_win32_collect_status(job)) {
		gettimeofday(&job->win32_exit_time, NULL);
		tv.tv_sec = 0;
		tv.tv_usec = WIN32_POLL_INTERVAL_MS * 1000;
		evtimer_add(&job->win32_poll_event, &tv);
		return;
	}
	if (was_closed && job_win32_collect_status(job)) {
		if (job->completecb != NULL)
			job->completecb(job);
		job_free(job);
		return;
	}
	if (job->state == JOB_DEAD) {
		if (job->win32_exit_time.tv_sec != 0 ||
		    job->win32_exit_time.tv_usec != 0) {
			gettimeofday(&now, NULL);
			timersub(&now, &job->win32_exit_time, &diff);
			if (diff.tv_sec == 0 &&
			    diff.tv_usec < WIN32_JOB_EXIT_GRACE_USEC) {
				tv.tv_sec = 0;
				tv.tv_usec = WIN32_POLL_INTERVAL_MS * 1000;
				evtimer_add(&job->win32_poll_event, &tv);
				return;
			}
		}
		if (job->completecb != NULL)
			job->completecb(job);
		job_free(job);
		return;
	}

	if (has_data) {
		job->win32_poll_idle_count = 0;
		tv.tv_sec = 0;
		tv.tv_usec = WIN32_POLL_INTERVAL_MS * 1000;
	} else {
		job->win32_poll_idle_count++;
		if (job->win32_poll_idle_count > WIN32_POLL_IDLE_THRESHOLD)
			tv.tv_sec = 0, tv.tv_usec = WIN32_POLL_IDLE_INTERVAL_MS * 1000;
		else
			tv.tv_sec = 0, tv.tv_usec = WIN32_POLL_INTERVAL_MS * 1000;
	}
	evtimer_add(&job->win32_poll_event, &tv);
}

static const char *
job_win32_get_shell(struct environ *env)
{
	struct environ_entry	*ee;

	ee = environ_find(env, "ComSpec");
	if (ee != NULL && ee->value != NULL && checkshell(ee->value))
		return (ee->value);
	ee = environ_find(env, "SHELL");
	if (ee != NULL && ee->value != NULL && checkshell(ee->value))
		return (ee->value);
	return (get_default_shell());
}

static void
job_win32_ensure_comspec(struct environ *env)
{
	if (environ_find(env, "ComSpec") == NULL)
		environ_set(env, "ComSpec", 0, "%s", job_win32_get_shell(env));
}

static char *
job_win32_resolve_cwd(struct environ *env, const char *cwd)
{
	char	*resolved;

	if (cwd == NULL)
		return (NULL);
	if (strncmp(cwd, "\\\\?\\", 4) == 0 &&
	    isalpha((u_char)cwd[4]) && cwd[5] == ':')
		resolved = xstrdup(cwd + 4);
	else if (strncmp(cwd, "\\\\?\\UNC\\", 8) == 0)
		xasprintf(&resolved, "\\\\%s", cwd + 8);
	else
		resolved = xstrdup(cwd);
	if (!path_is_directory(resolved) ||
	    !win32_spawn_cwd_is_process_supported(resolved)) {
		free(resolved);
		resolved = xstrdup(find_default_cwd());
	}
	environ_set(env, "PWD", 0, "%s", resolved);
	return (resolved);
}

static char **
job_win32_make_environment(struct environ *env, int *count)
{
	struct environ_entry	*ee;
	char			**environment;
	int			  n = 0, i = 0;

	for (ee = environ_first(env); ee != NULL; ee = environ_next(ee)) {
		if (ee->value != NULL && *ee->name != '\0' &&
		    (~ee->flags & ENVIRON_HIDDEN))
			n++;
	}

	environment = xcalloc(n == 0 ? 1 : n, sizeof *environment);
	for (ee = environ_first(env); ee != NULL; ee = environ_next(ee)) {
		if (ee->value == NULL || *ee->name == '\0' ||
		    (ee->flags & ENVIRON_HIDDEN))
			continue;
		xasprintf(&environment[i++], "%s=%s", ee->name, ee->value);
	}
	*count = n;
	return (environment);
}

static void
job_win32_free_environment(char **environment, int count)
{
	int	i;

	for (i = 0; i < count; i++)
		free(environment[i]);
	free(environment);
}

static struct job *
job_run_win32_pty(const char *cmd, const char *shell, int argc, char **argv,
    struct environ *env, const char *cwd, job_update_cb updatecb,
    job_complete_cb completecb, job_free_cb freecb, void *data, int flags,
    int sx, int sy)
{
	struct win32_spawn_options	 options;
	struct win32_pty		*pty;
	struct job			*job;
	struct timeval			 tv = { .tv_sec = 0, .tv_usec = WIN32_POLL_INTERVAL_MS * 1000 };
	uintptr_t			 master;
	char				**environment;
	char				 *cmd_cwd = NULL, *resolved_cwd, *shell_argv[4];
	const char			 *process_cwd;
	int				  environment_count, spawn_argc;
	char				**spawn_argv;

	if (cmd != NULL) {
		job_win32_ensure_comspec(env);
		spawn_argc = win32_shell_command_argv(shell, cmd,
		    shell_argv);
		spawn_argv = shell_argv;
	} else {
		if (argc <= 0 || argv == NULL) {
			environ_free(env);
			return (NULL);
		}
		spawn_argc = argc;
		spawn_argv = argv;
	}
	resolved_cwd = job_win32_resolve_cwd(env, cwd);
	process_cwd = resolved_cwd;
	if (win32_spawn_cwd_is_unc(resolved_cwd) && spawn_argc == 4 &&
	    win32_shell_is_cmd(spawn_argv[0]) &&
	    _stricmp(spawn_argv[1], "/d") == 0 &&
	    (_stricmp(spawn_argv[2], "/c") == 0 ||
	    _stricmp(spawn_argv[2], "/k") == 0)) {
		cmd_cwd = win32_spawn_cmd_pushd(resolved_cwd, spawn_argv[3]);
		shell_argv[0] = spawn_argv[0];
		shell_argv[1] = spawn_argv[1];
		shell_argv[2] = spawn_argv[2];
		shell_argv[3] = cmd_cwd;
		spawn_argv = shell_argv;
		process_cwd = find_default_cwd();
	}

	environment = job_win32_make_environment(env, &environment_count);
	pty = xcalloc(1, sizeof *pty);

	memset(&options, 0, sizeof options);
	options.argc = spawn_argc;
	options.argv = spawn_argv;
	options.cwd = process_cwd;
	options.environment = (const char *const *)environment;
	options.environment_count = environment_count;
	options.columns = sx <= 0 ? 80 : sx;
	options.rows = sy <= 0 ? 24 : sy;

	if (win32_spawn_pty(&options, pty, &master) != 0) {
		free(pty);
		free(cmd_cwd);
		free(resolved_cwd);
		job_win32_free_environment(environment, environment_count);
		environ_free(env);
		return (NULL);
	}
	free(cmd_cwd);
	free(resolved_cwd);
	job_win32_free_environment(environment, environment_count);
	environ_free(env);

	if (win32_socket_set_blocking(master, 0) != 0) {
		win32_pty_terminate(pty, 1);
		win32_socket_close(master);
		win32_pty_close(pty);
		free(pty);
		return (NULL);
	}

	job = xcalloc(1, sizeof *job);
	job->state = JOB_RUNNING;
	job->flags = flags;
	if (cmd != NULL)
		job->cmd = xstrdup(cmd);
	else
		job->cmd = cmd_stringify_argv(argc, argv);
	job->pid = (pid_t)win32_pty_process_id(pty);
	xsnprintf(job->tty, sizeof job->tty, "conpty:%lu",
	    win32_pty_process_id(pty));
	job->fd = -1;
	job->win32_socket = master;
	job->win32_pty = pty;

	LIST_INSERT_HEAD(&all_jobs, job, entry);
	job->updatecb = updatecb;
	job->completecb = completecb;
	job->freecb = freecb;
	job->data = data;

	job->event = bufferevent_new((evutil_socket_t)master, job_read_callback,
	    job_write_callback, job_error_callback, job);
	if (job->event == NULL)
		fatalx("out of memory");
	bufferevent_enable(job->event, EV_READ|EV_WRITE);
	evtimer_set(&job->win32_poll_event, job_win32_poll_callback, job);
	evtimer_add(&job->win32_poll_event, &tv);

	log_debug("run Windows job %p: %s, pid %ld", job, job->cmd,
	    (long)job->pid);
	return (job);
}

static struct job *
job_run_win32_process(const char *cmd, const char *shell, int argc, char **argv,
    struct environ *env, const char *cwd, job_update_cb updatecb,
    job_complete_cb completecb, job_free_cb freecb, void *data, int flags)
{
	struct win32_spawn_options	 options;
	struct win32_process		*process;
	struct job			*job;
	struct timeval			 tv = { .tv_sec = 0, .tv_usec = WIN32_POLL_INTERVAL_MS * 1000 };
	uintptr_t			 master;
	char				**environment;
	char				 *cmd_cwd = NULL, *resolved_cwd, *shell_argv[4];
	const char			 *process_cwd;
	int				  environment_count, spawn_argc;
	char				**spawn_argv;

	if (cmd != NULL) {
		job_win32_ensure_comspec(env);
		spawn_argc = win32_shell_command_argv(shell, cmd,
		    shell_argv);
		spawn_argv = shell_argv;
	} else {
		if (argc <= 0 || argv == NULL) {
			environ_free(env);
			return (NULL);
		}
		spawn_argc = argc;
		spawn_argv = argv;
	}
	resolved_cwd = job_win32_resolve_cwd(env, cwd);
	process_cwd = resolved_cwd;
	if (win32_spawn_cwd_is_unc(resolved_cwd) && spawn_argc == 4 &&
	    win32_shell_is_cmd(spawn_argv[0]) &&
	    _stricmp(spawn_argv[1], "/d") == 0 &&
	    (_stricmp(spawn_argv[2], "/c") == 0 ||
	    _stricmp(spawn_argv[2], "/k") == 0)) {
		cmd_cwd = win32_spawn_cmd_pushd(resolved_cwd, spawn_argv[3]);
		shell_argv[0] = spawn_argv[0];
		shell_argv[1] = spawn_argv[1];
		shell_argv[2] = spawn_argv[2];
		shell_argv[3] = cmd_cwd;
		spawn_argv = shell_argv;
		process_cwd = find_default_cwd();
	}

	environment = job_win32_make_environment(env, &environment_count);
	process = xcalloc(1, sizeof *process);

	memset(&options, 0, sizeof options);
	options.argc = spawn_argc;
	options.argv = spawn_argv;
	options.cwd = process_cwd;
	options.environment = (const char *const *)environment;
	options.environment_count = environment_count;

	if (win32_spawn_process(&options, process, &master,
	    !!(flags & JOB_SHOWSTDERR)) != 0) {
		free(process);
		free(cmd_cwd);
		free(resolved_cwd);
		job_win32_free_environment(environment, environment_count);
		environ_free(env);
		return (NULL);
	}
	free(cmd_cwd);
	free(resolved_cwd);
	job_win32_free_environment(environment, environment_count);
	environ_free(env);

	if (win32_socket_set_blocking(master, 0) != 0) {
		win32_process_terminate(process, 1);
		win32_socket_close(master);
		win32_process_close(process);
		free(process);
		return (NULL);
	}

	job = xcalloc(1, sizeof *job);
	job->state = JOB_RUNNING;
	job->flags = flags;
	if (cmd != NULL)
		job->cmd = xstrdup(cmd);
	else
		job->cmd = cmd_stringify_argv(argc, argv);
	job->pid = (pid_t)win32_process_id(process);
	job->fd = -1;
	job->win32_socket = master;
	job->win32_process = process;

	LIST_INSERT_HEAD(&all_jobs, job, entry);
	job->updatecb = updatecb;
	job->completecb = completecb;
	job->freecb = freecb;
	job->data = data;

	job->event = bufferevent_new((evutil_socket_t)master, job_read_callback,
	    job_write_callback, job_error_callback, job);
	if (job->event == NULL)
		fatalx("out of memory");
	bufferevent_enable(job->event, EV_READ|EV_WRITE);
	evtimer_set(&job->win32_poll_event, job_win32_poll_callback, job);
	evtimer_add(&job->win32_poll_event, &tv);

	log_debug("run Windows process job %p: %s, pid %ld", job, job->cmd,
	    (long)job->pid);
	return (job);
}
#endif

/* Start a job running. */
struct job *
job_run(const char *cmd, int argc, char **argv, struct environ *e,
    struct session *s, const char *cwd, job_update_cb updatecb,
    job_complete_cb completecb, job_free_cb freecb, void *data, int flags,
    int sx, int sy)
{
	struct job	 *job;
	struct environ	 *env;
#ifndef _WIN32
	pid_t		  pid;
	int		  nullfd, out[2], master, do_close = 1;
#endif
	const char	 *home, *shell;
#ifndef _WIN32
	sigset_t	  set, oldset;
	struct winsize	  ws;
	char		**argvp, tty[TTY_NAME_MAX], *argv0;
#endif
	struct options	 *oo;

	/*
	 * Do not set TERM during .tmux.conf (second argument here), it is nice
	 * to be able to use if-shell to decide on default-terminal based on
	 * outside TERM.
	 */
	env = environ_for_session(s, !cfg_finished);
	if (e != NULL)
		environ_copy(e, env);

#ifdef _WIN32
	if (~flags & JOB_DEFAULTSHELL)
		shell = job_win32_get_shell(env);
	else {
		if (s != NULL)
			oo = s->options;
		else
			oo = global_s_options;
		shell = options_get_string(oo, "default-shell");
		if (!checkshell(shell))
			shell = job_win32_get_shell(env);
	}
	environ_set(env, "SHELL", 0, "%s", shell);
#else
	if (~flags & JOB_DEFAULTSHELL)
		shell = _PATH_BSHELL;
	else {
		if (s != NULL)
			oo = s->options;
		else
			oo = global_s_options;
		shell = options_get_string(oo, "default-shell");
		if (!checkshell(shell))
			shell = _PATH_BSHELL;
	}
#endif
#ifdef _WIN32
	if (flags & JOB_PTY) {
		return (job_run_win32_pty(cmd, shell, argc, argv, env, cwd,
		    updatecb, completecb, freecb, data, flags, sx, sy));
	}
	return (job_run_win32_process(cmd, shell, argc, argv, env, cwd, updatecb,
	    completecb, freecb, data, flags));
#else
	argv0 = NULL;

	argv0 = shell_argv0(shell, 0);

	sigfillset(&set);
	sigprocmask(SIG_BLOCK, &set, &oldset);

	if (flags & JOB_PTY) {
		memset(&ws, 0, sizeof ws);
		ws.ws_col = sx;
		ws.ws_row = sy;
		pid = fdforkpty(ptm_fd, &master, tty, NULL, &ws);
	} else {
		if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, out) != 0)
			goto fail;
		pid = fork();
	}
	if (cmd == NULL) {
		cmd_log_argv(argc, argv, "%s:", __func__);
		log_debug("%s: cwd=%s, shell=%s", __func__,
		    cwd == NULL ? "" : cwd, shell);
	} else {
		log_debug("%s: cmd=%s, cwd=%s, shell=%s", __func__, cmd,
		    cwd == NULL ? "" : cwd, shell);
	}

	switch (pid) {
	case -1:
		if (~flags & JOB_PTY) {
			close(out[0]);
			close(out[1]);
		}
		goto fail;
	case 0:
		proc_clear_signals(server_proc, 1);
		sigprocmask(SIG_SETMASK, &oldset, NULL);

		if (cwd != NULL) {
			if (chdir(cwd) == 0)
				environ_set(env, "PWD", 0, "%s", cwd);
			else if ((home = find_home()) != NULL && chdir(home) == 0)
				environ_set(env, "PWD", 0, "%s", home);
			else if (chdir("/") == 0)
				environ_set(env, "PWD", 0, "/");
			else
				fatal("chdir failed");
		}

		environ_push(env);
		environ_free(env);

		if (~flags & JOB_PTY) {
			if (dup2(out[1], STDIN_FILENO) == -1)
				fatal("dup2 failed");
			do_close = do_close && out[1] != STDIN_FILENO;
			if (dup2(out[1], STDOUT_FILENO) == -1)
				fatal("dup2 failed");
			do_close = do_close && out[1] != STDOUT_FILENO;
			if (flags & JOB_SHOWSTDERR) {
				if (dup2(out[1], STDERR_FILENO) == -1)
					fatal("dup2 failed");
				do_close = do_close && out[1] != STDERR_FILENO;
			} else {
				nullfd = open(_PATH_DEVNULL, O_RDWR);
				if (nullfd == -1)
					fatal("open failed");
				if (dup2(nullfd, STDERR_FILENO) == -1)
					fatal("dup2 failed");
				if (nullfd != STDERR_FILENO)
					close(nullfd);
			}
			if (do_close)
				close(out[1]);
			close(out[0]);
		}
		closefrom(STDERR_FILENO + 1);

		if (cmd != NULL) {
			if (flags & JOB_DEFAULTSHELL)
				setenv("SHELL", shell, 1);
			execl(shell, argv0, "-c", cmd, (char *)NULL);
			fatal("execl failed");
		} else {
			argvp = cmd_copy_argv(argc, argv);
			execvp(argvp[0], argvp);
			fatal("execvp failed");
		}
	}

	sigprocmask(SIG_SETMASK, &oldset, NULL);
	environ_free(env);
	free(argv0);

	job = xcalloc(1, sizeof *job);
	job->state = JOB_RUNNING;
	job->flags = flags;

	if (cmd != NULL)
		job->cmd = xstrdup(cmd);
	else
		job->cmd = cmd_stringify_argv(argc, argv);
	job->pid = pid;
	if (flags & JOB_PTY)
		strlcpy(job->tty, tty, sizeof job->tty);
	job->status = 0;

	LIST_INSERT_HEAD(&all_jobs, job, entry);

	job->updatecb = updatecb;
	job->completecb = completecb;
	job->freecb = freecb;
	job->data = data;

	if (~flags & JOB_PTY) {
		close(out[1]);
		job->fd = out[0];
	} else
		job->fd = master;
	setblocking(job->fd, 0);

	job->event = bufferevent_new(job->fd, job_read_callback,
	    job_write_callback, job_error_callback, job);
	if (job->event == NULL)
		fatalx("out of memory");
	bufferevent_enable(job->event, EV_READ|EV_WRITE);

	log_debug("run job %p: %s, pid %ld", job, job->cmd, (long)job->pid);
	return (job);

fail:
	sigprocmask(SIG_SETMASK, &oldset, NULL);
	environ_free(env);
	free(argv0);
	return (NULL);
#endif
}

/* Take job's file descriptor and free the job. */
int
job_transfer(struct job *job, pid_t *pid, char *tty, size_t ttylen)
{
	int	fd = job->fd;

	log_debug("transfer job %p: %s", job, job->cmd);

	if (pid != NULL)
		*pid = job->pid;
	if (tty != NULL)
		strlcpy(tty, job->tty, ttylen);

#ifdef _WIN32
	if (job->win32_pty != NULL) {
		fd = -1;
		if (job->pid != -1)
			win32_pty_terminate(job->win32_pty, 1);
		if (job->event != NULL)
			bufferevent_free(job->event);
		win32_socket_close(job->win32_socket);
		win32_pty_close(job->win32_pty);
		free(job->win32_pty);
		job->event = NULL;
	}
	if (job->win32_process != NULL) {
		fd = -1;
		if (job->pid != -1)
			win32_process_terminate(job->win32_process, 1);
		if (job->event != NULL)
			bufferevent_free(job->event);
		win32_socket_close(job->win32_socket);
		win32_process_close(job->win32_process);
		free(job->win32_process);
		job->event = NULL;
	}
#endif

	LIST_REMOVE(job, entry);
	free(job->cmd);

	if (job->freecb != NULL && job->data != NULL)
		job->freecb(job->data);

	if (job->event != NULL)
		bufferevent_free(job->event);

	free(job);
	return (fd);
}

#ifdef _WIN32
/* Take job's Windows pty and socket and move them to a pane. */
int
job_transfer_win32(struct job *job, struct window_pane *wp, pid_t *pid,
    char *tty, size_t ttylen)
{
	log_debug("transfer Windows job %p: %s", job, job->cmd);

	if (job->win32_pty == NULL)
		return (-1);

	if (pid != NULL)
		*pid = job->pid;
	if (tty != NULL)
		strlcpy(tty, job->tty, ttylen);

	wp->fd = -1;
	wp->win32_socket = job->win32_socket;
	wp->win32_pty = job->win32_pty;

	LIST_REMOVE(job, entry);
	free(job->cmd);

	if (job->freecb != NULL && job->data != NULL)
		job->freecb(job->data);

	if (event_initialized(&job->win32_poll_event))
		event_del(&job->win32_poll_event);
	if (job->event != NULL)
		bufferevent_free(job->event);

	job->win32_socket = (uintptr_t)-1;
	job->win32_pty = NULL;
	free(job);
	return (0);
}
#endif

/* Kill and free an individual job. */
void
job_free(struct job *job)
{
	log_debug("free job %p: %s", job, job->cmd);

	LIST_REMOVE(job, entry);
	free(job->cmd);

	if (job->freecb != NULL && job->data != NULL)
		job->freecb(job->data);

#ifdef _WIN32
	if (job->win32_pty != NULL) {
		if (event_initialized(&job->win32_poll_event))
			event_del(&job->win32_poll_event);
		if (job->pid != -1)
			win32_pty_terminate(job->win32_pty, 1);
		if (job->event != NULL)
			bufferevent_free(job->event);
		win32_socket_close(job->win32_socket);
		win32_pty_close(job->win32_pty);
		free(job->win32_pty);
		free(job);
		return;
	}
	if (job->win32_process != NULL) {
		if (event_initialized(&job->win32_poll_event))
			event_del(&job->win32_poll_event);
		if (job->pid != -1)
			win32_process_terminate(job->win32_process, 1);
		if (job->event != NULL)
			bufferevent_free(job->event);
		win32_socket_close(job->win32_socket);
		win32_process_close(job->win32_process);
		free(job->win32_process);
		free(job);
		return;
	}
#endif

#ifndef _WIN32
	if (job->pid != -1)
		kill(job->pid, SIGTERM);
	if (job->event != NULL)
		bufferevent_free(job->event);
	if (job->fd != -1)
		close(job->fd);
#endif

	free(job);
}

/* Resize job. */
void
job_resize(struct job *job, u_int sx, u_int sy)
{
#ifndef _WIN32
	struct winsize	 ws;
#endif

#ifdef _WIN32
	if (job->win32_pty != NULL) {
		if (~job->flags & JOB_PTY)
			return;
		log_debug("resize Windows job %p: %ux%u", job, sx, sy);
		if (win32_pty_resize(job->win32_pty, sx, sy) != 0)
			fatal("win32_pty_resize failed");
		return;
	}
#endif

#ifndef _WIN32
	if (job->fd == -1 || (~job->flags & JOB_PTY))
		return;

	log_debug("resize job %p: %ux%u", job, sx, sy);

	memset(&ws, 0, sizeof ws);
	ws.ws_col = sx;
	ws.ws_row = sy;
	if (ioctl(job->fd, TIOCSWINSZ, &ws) == -1)
		fatal("ioctl failed");
#endif
}

/* Job buffer read callback. */
static void
job_read_callback(__unused struct bufferevent *bufev, void *data)
{
	struct job	*job = data;

	if (job->updatecb != NULL)
		job->updatecb(job);
}

/*
 * Job buffer write callback. Fired when the buffer falls below watermark
 * (default is empty). If all the data has been written, disable the write
 * event.
 */
static void
job_write_callback(__unused struct bufferevent *bufev, void *data)
{
	struct job	*job = data;
	size_t		 len = EVBUFFER_LENGTH(EVBUFFER_OUTPUT(job->event));

	log_debug("job write %p: %s, pid %ld, output left %zu", job, job->cmd,
	    (long) job->pid, len);

	if (len == 0 && (~job->flags & JOB_KEEPWRITE)) {
#ifdef _WIN32
		if (job->win32_pty != NULL)
			win32_socket_shutdown(job->win32_socket, 1);
		else if (job->win32_process != NULL)
			win32_socket_shutdown(job->win32_socket, 1);
#else
		shutdown(job->fd, SHUT_WR);
#endif
		bufferevent_disable(job->event, EV_WRITE);
	}
}

/* Job buffer error callback. */
static void
job_error_callback(__unused struct bufferevent *bufev, __unused short events,
    void *data)
{
	struct job	*job = data;

	log_debug("job error %p: %s, pid %ld", job, job->cmd, (long) job->pid);

#ifdef _WIN32
	job_win32_collect_status(job);
#endif

	if (job->state == JOB_DEAD) {
		if (job->completecb != NULL)
			job->completecb(job);
		job_free(job);
	} else {
		bufferevent_disable(job->event, EV_READ);
		job->state = JOB_CLOSED;
	}
}

/* Job died (waitpid() returned its pid). */
void
job_check_died(pid_t pid, int status)
{
#ifdef _WIN32
	(void)pid;
	(void)status;
#else
	struct job	*job;

	LIST_FOREACH(job, &all_jobs, entry) {
		if (pid == job->pid)
			break;
	}
	if (job == NULL)
		return;
	if (WIFSTOPPED(status)) {
		if (WSTOPSIG(status) == SIGTTIN || WSTOPSIG(status) == SIGTTOU)
			return;
		killpg(job->pid, SIGCONT);
		return;
	}
	log_debug("job died %p: %s, pid %ld", job, job->cmd, (long) job->pid);

	job->status = status;

	if (job->state == JOB_CLOSED) {
		if (job->completecb != NULL)
			job->completecb(job);
		job_free(job);
	} else {
		job->pid = -1;
		job->state = JOB_DEAD;
	}
#endif
}

/* Get job status. */
int
job_get_status(struct job *job)
{
	return (job->status);
}

/* Get job data. */
void *
job_get_data(struct job *job)
{
	return (job->data);
}

/* Get job event. */
struct bufferevent *
job_get_event(struct job *job)
{
	return (job->event);
}

/* Kill all jobs. */
void
job_kill_all(void)
{
	struct job	*job;

	LIST_FOREACH(job, &all_jobs, entry) {
#ifdef _WIN32
		if (job->win32_pty != NULL) {
			if (job->pid != -1)
				win32_pty_terminate(job->win32_pty, 1);
			continue;
		}
		if (job->win32_process != NULL) {
			if (job->pid != -1)
				win32_process_terminate(job->win32_process, 1);
			continue;
		}
#endif
#ifndef _WIN32
		if (job->pid != -1)
			kill(job->pid, SIGTERM);
#endif
	}
}

/* Are any jobs still running? */
int
job_still_running(void)
{
	struct job	*job;

	LIST_FOREACH(job, &all_jobs, entry) {
		if ((~job->flags & JOB_NOWAIT) && job->state == JOB_RUNNING)
			return (1);
	}
	return (0);
}

/* Print job summary. */
void
job_print_summary(struct cmdq_item *item, int blank)
{
	struct job	*job;
	u_int		 n = 0;

	LIST_FOREACH(job, &all_jobs, entry) {
		if (blank) {
			cmdq_print(item, "%s", "");
			blank = 0;
		}
		cmdq_print(item, "Job %u: %s [fd=%d, pid=%ld, status=%d]",
		    n, job->cmd, job->fd, (long)job->pid, job->status);
		n++;
	}
}
