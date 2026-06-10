/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicholas.marriott@gmail.com>
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
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/file.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#ifdef _WIN32
#include <process.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

#ifdef _WIN32
#include "compat/win32-command.h"
#include "compat/win32-handle.h"
#include "compat/win32-ipc.h"
#include "compat/win32-job.h"
#include "compat/win32-socketpair.h"
#endif

static struct tmuxproc	*client_proc;
static struct tmuxpeer	*client_peer;
static struct event_base *client_base;
static uint64_t		 client_flags;
static int		 client_suspended;
static enum {
	CLIENT_EXIT_NONE,
	CLIENT_EXIT_DETACHED,
	CLIENT_EXIT_DETACHED_HUP,
	CLIENT_EXIT_LOST_TTY,
	CLIENT_EXIT_TERMINATED,
	CLIENT_EXIT_LOST_SERVER,
	CLIENT_EXIT_EXITED,
	CLIENT_EXIT_SERVER_EXITED,
	CLIENT_EXIT_MESSAGE_PROVIDED
} client_exitreason = CLIENT_EXIT_NONE;
static int		 client_exitflag;
static int		 client_exitval;
static enum msgtype	 client_exittype;
static const char	*client_exitsession;
static char		*client_exitmessage;
static const char	*client_execshell;
static const char	*client_execcmd;
static int		 client_attached;
static struct client_files client_files = RB_INITIALIZER(&client_files);
#ifdef _WIN32
#define CLIENT_WIN32_STDIN_BUFFER 8192
#define CLIENT_WIN32_STDOUT_BUFFER 8192
#define CLIENT_WIN32_STDIN_OK 0
#define CLIENT_WIN32_STDIN_INVALID_HANDLE 1
#define CLIENT_WIN32_STDIN_READ_FAILED 2
#define CLIENT_WIN32_STDIN_EOF 3
#define CLIENT_WIN32_STDIN_SEND_FAILED 4
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
static struct event	 client_win32_resize_event;
static int		 client_win32_resize_event_set;
static u_int		 client_win32_resize_sx;
static u_int		 client_win32_resize_sy;
static struct event	 client_win32_stdin_event;
static int		 client_win32_stdin_event_set;
static uintptr_t	 client_win32_stdin_socket = (uintptr_t)INVALID_SOCKET;
static uintptr_t	 client_win32_stdin_bridge_socket =
			     (uintptr_t)INVALID_SOCKET;
static HANDLE		 client_win32_stdin_thread;
static volatile LONG	 client_win32_stdin_status;
static volatile LONG	 client_win32_stdin_error;
static HANDLE		 client_win32_stdout_read = INVALID_HANDLE_VALUE;
static HANDLE		 client_win32_stdout_write = INVALID_HANDLE_VALUE;
static HANDLE		 client_win32_stdout_thread;
static DWORD		 client_win32_stdout_mode;
static int		 client_win32_stdout_mode_valid;
static UINT		 client_win32_stdout_codepage;
static int		 client_win32_stdout_codepage_valid;
static DWORD		 client_win32_stdin_mode;
static int		 client_win32_stdin_mode_valid;
static UINT		 client_win32_stdin_codepage;
static int		 client_win32_stdin_codepage_valid;
static struct event	 client_win32_signal_event;
static int		 client_win32_signal_event_set;
static uintptr_t	 client_win32_signal_socket = (uintptr_t)INVALID_SOCKET;
static uintptr_t	 client_win32_signal_bridge_socket =
			     (uintptr_t)INVALID_SOCKET;
#endif

static __dead void	 client_exec(const char *,const char *);
#ifndef _WIN32
static int		 client_get_lock(char *);
#endif
static imsg_fd_t	 client_connect(struct event_base *, const char *,
			     uint64_t);
static void		 client_send_identify(const char *, const char *,
			     char **, u_int, const char *, int);
static void		 client_signal(int);
static void		 client_dispatch(struct imsg *, void *);
static void		 client_dispatch_attached(struct imsg *);
static void		 client_dispatch_wait(struct imsg *);
static const char	*client_exit_message(void);
#ifdef _WIN32
static const char	*client_win32_lost_tty_message(char *, size_t);
static void		 client_win32_note_stdin_status(LONG, DWORD);
static DWORD WINAPI	 client_win32_stdin_thread_proc(LPVOID);
static void		 client_win32_stdin_callback(evutil_socket_t, short,
			     void *);
static void		 client_win32_start_stdin_proxy(void);
static void		 client_win32_stop_stdin_proxy(void);
static void		 client_win32_restore_stdin(void);
static DWORD WINAPI	 client_win32_stdout_thread_proc(LPVOID);
static int		 client_win32_prepare_stdout(HANDLE);
static void		 client_win32_restore_stdout(void);
static int		 client_win32_start_stdout_proxy(
			     struct win32_handle_message *);
static void		 client_win32_stop_stdout_proxy(void);
static void		 client_win32_signal_callback(evutil_socket_t, short,
			     void *);
static void		 client_win32_start_signal_proxy(void);
static void		 client_win32_stop_signal_proxy(void);
static void		 client_win32_send_signal_input(char);
static HANDLE		 client_win32_lock_start_server(const char *);
static void		 client_win32_unlock_start_server(HANDLE);
static int		 client_win32_start_server(const char *);
static int		 client_win32_retry_connect(const char *, uintptr_t *);
static int		 client_win32_get_size(u_int *, u_int *);
static void		 client_win32_send_resize(void);
static void		 client_win32_resize_timer(evutil_socket_t, short, void *);
static void		 client_win32_start_resize_timer(void);
#endif

/*
 * Get server create lock. If already held then server start is happening in
 * another client, so block until the lock is released and return -2 to
 * retry. Return -1 on failure to continue and start the server anyway.
 */
#ifndef _WIN32
static int
client_get_lock(char *lockfile)
{
	int lockfd;

	log_debug("lock file is %s", lockfile);

	if ((lockfd = open(lockfile, O_WRONLY|O_CREAT, 0600)) == -1) {
		log_debug("open failed: %s", strerror(errno));
		return (-1);
	}

	if (flock(lockfd, LOCK_EX|LOCK_NB) == -1) {
		log_debug("flock failed: %s", strerror(errno));
		if (errno != EAGAIN)
			return (lockfd);
		while (flock(lockfd, LOCK_EX) == -1 && errno == EINTR)
			/* nothing */;
		close(lockfd);
		return (-2);
	}
	log_debug("flock succeeded");

	return (lockfd);
}
#endif

#ifdef _WIN32
static uint64_t
client_win32_hash_path(const char *path)
{
	uint64_t	hash = 1469598103934665603ULL;

	for (; *path != '\0'; path++) {
		hash ^= (u_char)*path;
		hash *= 1099511628211ULL;
	}
	return (hash);
}

static HANDLE
client_win32_lock_start_server(const char *path)
{
	uint64_t	hash;
	wchar_t		name[64];
	HANDLE		mutex;
	DWORD		wait;

	hash = client_win32_hash_path(path);
	swprintf(name, nitems(name), L"Local\\tmux-start-%08lx%08lx",
	    (unsigned long)(hash >> 32), (unsigned long)hash);

	mutex = CreateMutexW(NULL, FALSE, name);
	if (mutex == NULL) {
		errno = EIO;
		return (NULL);
	}
	wait = WaitForSingleObject(mutex, 10000);
	if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
		return (mutex);

	CloseHandle(mutex);
	errno = ETIMEDOUT;
	return (NULL);
}

static void
client_win32_unlock_start_server(HANDLE mutex)
{
	if (mutex == NULL)
		return;
	ReleaseMutex(mutex);
	CloseHandle(mutex);
}

static void
client_win32_remove_endpoint(const char *path)
{
	wchar_t	*wpath;

	wpath = win32_utf8_to_wide(path);
	if (wpath == NULL)
		return;
	DeleteFileW(wpath);
	free(wpath);
}

static int
client_win32_start_server(const char *path)
{
	STARTUPINFOW		 si;
	PROCESS_INFORMATION	 pi;
	wchar_t			 module[MAX_PATH], *wpath, *command_line;
	wchar_t		       **cfg_wide = NULL;
	const wchar_t	       **argv;
	DWORD			 flags, module_len;
	u_int			 i;
	int			 argc, idx, log_level;

	module_len = GetModuleFileNameW(NULL, module, nitems(module));
	if (module_len == 0 || module_len >= nitems(module)) {
		errno = EIO;
		return (-1);
	}
	wpath = win32_utf8_to_wide(path);
	if (wpath == NULL) {
		errno = EINVAL;
		return (-1);
	}

	log_level = log_get_level();
	argc = 4 + log_level;
	if (cfg_user_files)
		argc += 2 * cfg_nfiles;
	argv = xcalloc(argc, sizeof *argv);

	idx = 0;
	argv[idx++] = module;
	while (log_level-- > 0)
		argv[idx++] = L"-v";
	argv[idx++] = L"-D";
	argv[idx++] = L"-S";
	argv[idx++] = wpath;
	if (cfg_user_files) {
		cfg_wide = xcalloc(cfg_nfiles, sizeof *cfg_wide);
		for (i = 0; i < cfg_nfiles; i++) {
			cfg_wide[i] = win32_utf8_to_wide(cfg_files[i]);
			if (cfg_wide[i] == NULL) {
				command_line = NULL;
				goto out;
			}
			argv[idx++] = L"-f";
			argv[idx++] = cfg_wide[i];
		}
	}

	command_line = win32_build_command_line_wide(argc, argv);
out:
	for (i = 0; cfg_wide != NULL && i < cfg_nfiles; i++)
		free(cfg_wide[i]);
	free(cfg_wide);
	free((void *)argv);
	free(wpath);
	if (command_line == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	memset(&si, 0, sizeof si);
	memset(&pi, 0, sizeof pi);
	si.cb = sizeof si;
	flags = CREATE_NO_WINDOW|CREATE_NEW_PROCESS_GROUP|DETACHED_PROCESS|
	    win32_job_creation_flags_for_child();
	if (!CreateProcessW(module, command_line, NULL, NULL, FALSE, flags,
	    NULL, NULL, &si, &pi)) {
		free(command_line);
		errno = EIO;
		return (-1);
	}
	free(command_line);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return (0);
}

static int
client_win32_retry_connect(const char *path, uintptr_t *fd)
{
	int	i, error;

	for (i = 0; i < 50; i++) {
		if (win32_ipc_connect(path, fd) == 0) {
			win32_socket_set_blocking(*fd, 0);
			return (0);
		}
		error = WSAGetLastError();
		if (error != WSAECONNREFUSED &&
		    GetLastError() != ERROR_FILE_NOT_FOUND &&
		    GetLastError() != ERROR_PATH_NOT_FOUND)
			break;
		Sleep(100);
	}
	return (-1);
}
#endif

/* Connect client to server. */
static imsg_fd_t
client_connect(struct event_base *base, const char *path, uint64_t flags)
{
#ifdef _WIN32
	HANDLE		mutex = NULL;
	uintptr_t	fd;
	int		error;

	(void)base;
	log_debug("socket endpoint is %s", path);

	if (win32_ipc_connect(path, &fd) != 0) {
		error = WSAGetLastError();
		log_debug("connect failed: Windows error %lu, Winsock error %d",
		    GetLastError(), error);
		if ((flags & CLIENT_NOFORK) &&
		    (flags & CLIENT_STARTSERVER) &&
		    (~flags & CLIENT_NOSTARTSERVER))
			return ((imsg_fd_t)server_start(client_proc, flags,
			    base, -1, NULL));
		if (error == WSAECONNREFUSED)
			errno = ECONNREFUSED;
		else if (GetLastError() == ERROR_FILE_NOT_FOUND ||
		    GetLastError() == ERROR_PATH_NOT_FOUND)
			errno = ENOENT;
		else
			errno = EIO;
		if ((flags & CLIENT_STARTSERVER) &&
		    (~flags & CLIENT_NOSTARTSERVER)) {
			mutex = client_win32_lock_start_server(path);
			if (mutex == NULL)
				return ((imsg_fd_t)-1);
			if (win32_ipc_connect(path, &fd) == 0) {
				win32_socket_set_blocking(fd, 0);
				client_win32_unlock_start_server(mutex);
				return (fd);
			}
			client_win32_remove_endpoint(path);
			if (client_win32_start_server(path) == 0 &&
			    client_win32_retry_connect(path, &fd) == 0) {
				client_win32_unlock_start_server(mutex);
				return (fd);
			}
			client_win32_unlock_start_server(mutex);
			errno = EIO;
		}
		return ((imsg_fd_t)-1);
	}
	win32_socket_set_blocking(fd, 0);
	return (fd);
#else
	struct sockaddr_un	sa;
	size_t			size;
	int			fd, lockfd = -1, locked = 0;
	char		       *lockfile = NULL;

	memset(&sa, 0, sizeof sa);
	sa.sun_family = AF_UNIX;
	size = strlcpy(sa.sun_path, path, sizeof sa.sun_path);
	if (size >= sizeof sa.sun_path) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	log_debug("socket is %s", path);

retry:
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		return (-1);

	log_debug("trying connect");
	if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == -1) {
		log_debug("connect failed: %s", strerror(errno));
		if (errno != ECONNREFUSED && errno != ENOENT)
			goto failed;
		if (flags & CLIENT_NOSTARTSERVER)
			goto failed;
		if (~flags & CLIENT_STARTSERVER)
			goto failed;
		close(fd);

		if (!locked) {
			xasprintf(&lockfile, "%s.lock", path);
			if ((lockfd = client_get_lock(lockfile)) < 0) {
				log_debug("didn't get lock (%d)", lockfd);

				free(lockfile);
				lockfile = NULL;

				if (lockfd == -2)
					goto retry;
			}
			log_debug("got lock (%d)", lockfd);

			/*
			 * Always retry at least once, even if we got the lock,
			 * because another client could have taken the lock,
			 * started the server and released the lock between our
			 * connect() and flock().
			 */
			locked = 1;
			goto retry;
		}

		if (lockfd >= 0 && unlink(path) != 0 && errno != ENOENT) {
			free(lockfile);
			close(lockfd);
			return (-1);
		}
		fd = server_start(client_proc, flags, base, lockfd, lockfile);
	}

	if (locked && lockfd >= 0) {
		free(lockfile);
		close(lockfd);
	}
	setblocking(fd, 0);
	return (fd);

failed:
	if (locked) {
		free(lockfile);
		close(lockfd);
	}
	close(fd);
	return (-1);
#endif
}

/* Get exit string from reason number. */
const char *
client_exit_message(void)
{
	static char msg[256];

	switch (client_exitreason) {
	case CLIENT_EXIT_NONE:
		break;
	case CLIENT_EXIT_DETACHED:
		if (client_exitsession != NULL) {
			xsnprintf(msg, sizeof msg, "detached "
			    "(from session %s)", client_exitsession);
			return (msg);
		}
		return ("detached");
	case CLIENT_EXIT_DETACHED_HUP:
		if (client_exitsession != NULL) {
			xsnprintf(msg, sizeof msg, "detached and SIGHUP "
			    "(from session %s)", client_exitsession);
			return (msg);
		}
		return ("detached and SIGHUP");
	case CLIENT_EXIT_LOST_TTY:
#ifdef _WIN32
		return (client_win32_lost_tty_message(msg, sizeof msg));
#else
		return ("lost tty");
#endif
	case CLIENT_EXIT_TERMINATED:
		return ("terminated");
	case CLIENT_EXIT_LOST_SERVER:
		return ("server exited unexpectedly");
	case CLIENT_EXIT_EXITED:
		return ("exited");
	case CLIENT_EXIT_SERVER_EXITED:
		return ("server exited");
	case CLIENT_EXIT_MESSAGE_PROVIDED:
		return (client_exitmessage);
	}
	return ("unknown reason");
}

#ifdef _WIN32
static const char *
client_win32_lost_tty_message(char *msg, size_t msglen)
{
	LONG	status, error;

	status = InterlockedCompareExchange(&client_win32_stdin_status, 0, 0);
	error = InterlockedCompareExchange(&client_win32_stdin_error, 0, 0);
	switch (status) {
	case CLIENT_WIN32_STDIN_INVALID_HANDLE:
		xsnprintf(msg, msglen, "lost tty "
		    "(Windows stdin handle invalid, error %ld)", error);
		return (msg);
	case CLIENT_WIN32_STDIN_READ_FAILED:
		xsnprintf(msg, msglen, "lost tty "
		    "(Windows stdin ReadFile failed, error %ld)", error);
		return (msg);
	case CLIENT_WIN32_STDIN_EOF:
		return ("lost tty (Windows stdin closed)");
	case CLIENT_WIN32_STDIN_SEND_FAILED:
		xsnprintf(msg, msglen, "lost tty "
		    "(Windows stdin proxy send failed, error %ld)", error);
		return (msg);
	default:
		return ("lost tty");
	}
}

static void
client_win32_note_stdin_status(LONG status, DWORD error)
{
	InterlockedExchange(&client_win32_stdin_error, (LONG)error);
	InterlockedExchange(&client_win32_stdin_status, status);
}
#endif

/* Exit if all streams flushed. */
static void
client_exit(void)
{
	if (!file_write_left(&client_files))
		proc_exit(client_proc);
}

#ifdef _WIN32
static DWORD WINAPI
client_win32_stdin_thread_proc(LPVOID data)
{
	uintptr_t	 socket = (uintptr_t)data;
	HANDLE		 input;
	char		 buffer[CLIENT_WIN32_STDIN_BUFFER];
	DWORD		 read;
	int		 sent, offset;

	input = GetStdHandle(STD_INPUT_HANDLE);
	if (input == INVALID_HANDLE_VALUE || input == NULL) {
		client_win32_note_stdin_status(CLIENT_WIN32_STDIN_INVALID_HANDLE,
		    GetLastError());
		goto out;
	}

	for (;;) {
		if (!ReadFile(input, buffer, sizeof buffer, &read, NULL)) {
			client_win32_note_stdin_status(
			    CLIENT_WIN32_STDIN_READ_FAILED, GetLastError());
			break;
		}
		if (read == 0) {
			Sleep(1);
			continue;
		}
		offset = 0;
		while (offset < (int)read) {
			sent = send((SOCKET)socket, buffer + offset,
			    (int)read - offset, 0);
			if (sent <= 0) {
				client_win32_note_stdin_status(
				    CLIENT_WIN32_STDIN_SEND_FAILED,
				    WSAGetLastError());
				goto out;
			}
			offset += sent;
		}
	}

out:
	win32_socket_shutdown(socket, 1);
	return (0);
}

static void
client_win32_stdin_callback(__unused evutil_socket_t fd,
    __unused short events, __unused void *data)
{
	char	buffer[CLIENT_WIN32_STDIN_BUFFER];
	int	n;

	n = recv((SOCKET)client_win32_stdin_socket, buffer, sizeof buffer, 0);
	if (n <= 0) {
		client_win32_stop_stdin_proxy();
		if (client_attached) {
			client_exitreason = CLIENT_EXIT_LOST_TTY;
			client_exitval = 1;
			proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		}
		return;
	}
	proc_send(client_peer, MSG_STDIN, -1, buffer, n);
}

static void
client_win32_start_stdin_proxy(void)
{
	uintptr_t	pair[2];
	HANDLE		input;
	DWORD		mode;
	UINT		cp;

	if (client_win32_stdin_event_set || (client_flags & CLIENT_CONTROL))
		return;

	input = GetStdHandle(STD_INPUT_HANDLE);
	if (input == INVALID_HANDLE_VALUE || input == NULL ||
	    !GetConsoleMode(input, &mode))
		return;

	/* Save original stdin mode and codepage for restore on exit. */
	if (!client_win32_stdin_mode_valid) {
		client_win32_stdin_mode = mode;
		client_win32_stdin_mode_valid = 1;
	}
	if (!client_win32_stdin_codepage_valid) {
		cp = GetConsoleCP();
		if (cp != 0) {
			client_win32_stdin_codepage = cp;
			client_win32_stdin_codepage_valid = 1;
		}
	}
	if (client_win32_stdin_codepage_valid)
		SetConsoleCP(CP_UTF8);

	/*
	 * Put the console into raw VT input mode so keyboard input is delivered
	 * to tmux as VT/UTF-8 sequences instead of being line-buffered, echoed
	 * or cooked by the console.
	 */
	mode &= ~(ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|
	    ENABLE_PROCESSED_INPUT);
	mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (!SetConsoleMode(input, mode)) {
		/* Fallback for older Windows without VT input support. */
		mode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
		SetConsoleMode(input, mode);
	}

	if (win32_socketpair(pair) != 0)
		return;
	client_win32_note_stdin_status(CLIENT_WIN32_STDIN_OK, 0);
	client_win32_stdin_socket = pair[0];
	client_win32_stdin_bridge_socket = pair[1];

	event_set(&client_win32_stdin_event,
	    (evutil_socket_t)client_win32_stdin_socket, EV_READ|EV_PERSIST,
	    client_win32_stdin_callback, NULL);
	event_base_set(client_base, &client_win32_stdin_event);
	event_add(&client_win32_stdin_event, NULL);
	client_win32_stdin_event_set = 1;

	client_win32_stdin_thread = CreateThread(NULL, 0,
	    client_win32_stdin_thread_proc,
	    (LPVOID)client_win32_stdin_bridge_socket, 0, NULL);
	if (client_win32_stdin_thread == NULL)
		client_win32_stop_stdin_proxy();
}

static void
client_win32_restore_stdin(void)
{
	HANDLE	input;

	input = GetStdHandle(STD_INPUT_HANDLE);
	if (client_win32_stdin_mode_valid &&
	    input != INVALID_HANDLE_VALUE && input != NULL) {
		SetConsoleMode(input, client_win32_stdin_mode);
		client_win32_stdin_mode_valid = 0;
	}
	if (client_win32_stdin_codepage_valid) {
		SetConsoleCP(client_win32_stdin_codepage);
		client_win32_stdin_codepage_valid = 0;
	}
}

static void
client_win32_stop_stdin_proxy(void)
{
	if (client_win32_stdin_event_set) {
		event_del(&client_win32_stdin_event);
		client_win32_stdin_event_set = 0;
	}
	if (client_win32_stdin_socket != (uintptr_t)INVALID_SOCKET) {
		win32_socket_shutdown(client_win32_stdin_socket, 0);
		win32_socket_close(client_win32_stdin_socket);
		client_win32_stdin_socket = (uintptr_t)INVALID_SOCKET;
	}
	if (client_win32_stdin_bridge_socket != (uintptr_t)INVALID_SOCKET) {
		win32_socket_shutdown(client_win32_stdin_bridge_socket, 0);
		win32_socket_close(client_win32_stdin_bridge_socket);
		client_win32_stdin_bridge_socket = (uintptr_t)INVALID_SOCKET;
	}
	if (client_win32_stdin_thread != NULL) {
		CancelSynchronousIo(client_win32_stdin_thread);
		WaitForSingleObject(client_win32_stdin_thread, 1000);
		CloseHandle(client_win32_stdin_thread);
		client_win32_stdin_thread = NULL;
	}
	client_win32_restore_stdin();
}

static DWORD WINAPI
client_win32_stdout_thread_proc(LPVOID data)
{
	HANDLE	 pipe = data, output;
	char	 buffer[CLIENT_WIN32_STDOUT_BUFFER];
	DWORD	 read, written;
	int	 offset;

	output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output == INVALID_HANDLE_VALUE || output == NULL)
		return (0);

	for (;;) {
		if (!ReadFile(pipe, buffer, sizeof buffer, &read, NULL))
			break;
		if (read == 0) {
			Sleep(1);
			continue;
		}
		offset = 0;
		while (offset < (int)read) {
			if (!WriteFile(output, buffer + offset,
			    read - offset, &written, NULL))
				return (0);
			if (written == 0)
				return (0);
			offset += (int)written;
		}
	}
	return (0);
}

static int
client_win32_prepare_stdout(HANDLE output)
{
	DWORD	mode;
	UINT	codepage;
	int	ok = 0;

	if (output == INVALID_HANDLE_VALUE || output == NULL ||
	    !GetConsoleMode(output, &mode))
		return (-1);

	if (!client_win32_stdout_mode_valid) {
		client_win32_stdout_mode = mode;
		client_win32_stdout_mode_valid = 1;
	}
	if (!client_win32_stdout_codepage_valid) {
		codepage = GetConsoleOutputCP();
		if (codepage != 0) {
			client_win32_stdout_codepage = codepage;
			client_win32_stdout_codepage_valid = 1;
		}
	}
	if (client_win32_stdout_codepage_valid)
		SetConsoleOutputCP(CP_UTF8);

	mode |= ENABLE_PROCESSED_OUTPUT|ENABLE_VIRTUAL_TERMINAL_PROCESSING|
	    DISABLE_NEWLINE_AUTO_RETURN;
	if (SetConsoleMode(output, mode))
		ok = 1;
	else if (SetConsoleMode(output, mode & ~DISABLE_NEWLINE_AUTO_RETURN))
		ok = 1;
	return (ok ? 0 : -1);
}

static void
client_win32_restore_stdout(void)
{
	HANDLE	output;

	output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (client_win32_stdout_mode_valid &&
	    output != INVALID_HANDLE_VALUE && output != NULL) {
		SetConsoleMode(output, client_win32_stdout_mode);
		client_win32_stdout_mode_valid = 0;
	}
	if (client_win32_stdout_codepage_valid) {
		SetConsoleOutputCP(client_win32_stdout_codepage);
		client_win32_stdout_codepage_valid = 0;
	}
}

static int
client_win32_start_stdout_proxy(struct win32_handle_message *handle)
{
	SECURITY_ATTRIBUTES	 attributes;
	HANDLE			 output;
	DWORD			 mode;

	if (handle == NULL)
		return (-1);
	if (client_win32_stdout_thread != NULL) {
		return (win32_handle_message_from_handle(
		    client_win32_stdout_write, 0, handle));
	}

	output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output == INVALID_HANDLE_VALUE || output == NULL ||
	    !GetConsoleMode(output, &mode))
		return (-1);

	client_win32_prepare_stdout(output);

	memset(&attributes, 0, sizeof attributes);
	attributes.nLength = sizeof attributes;
	attributes.bInheritHandle = FALSE;
	if (!CreatePipe(&client_win32_stdout_read, &client_win32_stdout_write,
	    &attributes, 0)) {
		client_win32_restore_stdout();
		return (-1);
	}
	if (win32_handle_message_from_handle(client_win32_stdout_write, 0,
	    handle) != 0) {
		client_win32_stop_stdout_proxy();
		return (-1);
	}

	client_win32_stdout_thread = CreateThread(NULL, 0,
	    client_win32_stdout_thread_proc, client_win32_stdout_read, 0, NULL);
	if (client_win32_stdout_thread == NULL) {
		client_win32_stop_stdout_proxy();
		return (-1);
	}
	return (0);
}

static void
client_win32_stop_stdout_proxy(void)
{
	if (client_win32_stdout_write != INVALID_HANDLE_VALUE) {
		CloseHandle(client_win32_stdout_write);
		client_win32_stdout_write = INVALID_HANDLE_VALUE;
	}
	if (client_win32_stdout_thread != NULL) {
		if (WaitForSingleObject(client_win32_stdout_thread, 1000) ==
		    WAIT_TIMEOUT) {
			CancelSynchronousIo(client_win32_stdout_thread);
			WaitForSingleObject(client_win32_stdout_thread, 1000);
		}
		CloseHandle(client_win32_stdout_thread);
		client_win32_stdout_thread = NULL;
	}
	if (client_win32_stdout_read != INVALID_HANDLE_VALUE) {
		CloseHandle(client_win32_stdout_read);
		client_win32_stdout_read = INVALID_HANDLE_VALUE;
	}
	client_win32_restore_stdout();
}

static void
client_win32_signal_callback(__unused evutil_socket_t fd,
    __unused short events, __unused void *data)
{
	char	buffer[32], ch;
	int	n, i;

	n = recv((SOCKET)client_win32_signal_socket, buffer,
	    sizeof buffer, 0);
	if (n <= 0) {
		client_win32_stop_signal_proxy();
		return;
	}

	for (i = 0; i < n; i++) {
		ch = buffer[i];
		if (client_attached && client_peer != NULL)
			proc_send(client_peer, MSG_STDIN, -1, &ch, 1);
		else if (!client_attached)
			proc_exit(client_proc);
	}
}

static void
client_win32_start_signal_proxy(void)
{
	uintptr_t	pair[2];

	if (client_win32_signal_event_set)
		return;
	if (win32_socketpair(pair) != 0)
		return;
	client_win32_signal_socket = pair[0];
	client_win32_signal_bridge_socket = pair[1];

	event_set(&client_win32_signal_event,
	    (evutil_socket_t)client_win32_signal_socket, EV_READ|EV_PERSIST,
	    client_win32_signal_callback, NULL);
	event_base_set(client_base, &client_win32_signal_event);
	event_add(&client_win32_signal_event, NULL);
	client_win32_signal_event_set = 1;
}

static void
client_win32_stop_signal_proxy(void)
{
	if (client_win32_signal_event_set) {
		event_del(&client_win32_signal_event);
		client_win32_signal_event_set = 0;
	}
	if (client_win32_signal_socket != (uintptr_t)INVALID_SOCKET) {
		win32_socket_shutdown(client_win32_signal_socket, 0);
		win32_socket_close(client_win32_signal_socket);
		client_win32_signal_socket = (uintptr_t)INVALID_SOCKET;
	}
	if (client_win32_signal_bridge_socket != (uintptr_t)INVALID_SOCKET) {
		win32_socket_shutdown(client_win32_signal_bridge_socket, 0);
		win32_socket_close(client_win32_signal_bridge_socket);
		client_win32_signal_bridge_socket = (uintptr_t)INVALID_SOCKET;
	}
}

static void
client_win32_send_signal_input(char ch)
{
	if (client_win32_signal_bridge_socket != (uintptr_t)INVALID_SOCKET)
		send((SOCKET)client_win32_signal_bridge_socket, &ch, 1, 0);
}

static int
client_win32_get_size(u_int *sx, u_int *sy)
{
	CONSOLE_SCREEN_BUFFER_INFO	info;
	HANDLE				output;

	output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (output == INVALID_HANDLE_VALUE || output == NULL)
		return (-1);
	if (!GetConsoleScreenBufferInfo(output, &info))
		return (-1);

	*sx = info.srWindow.Right - info.srWindow.Left + 1;
	*sy = info.srWindow.Bottom - info.srWindow.Top + 1;
	return (0);
}

static void
client_win32_send_resize(void)
{
	struct msg_resize	data;
	u_int			sx, sy;

	if (client_win32_get_size(&sx, &sy) != 0)
		return;
	data.sx = sx;
	data.sy = sy;
	proc_send(client_peer, MSG_RESIZE, -1, &data, sizeof data);
	client_win32_resize_sx = sx;
	client_win32_resize_sy = sy;
}

static void
client_win32_resize_timer(__unused evutil_socket_t fd, __unused short events,
    __unused void *data)
{
	struct timeval	tv = { .tv_sec = 1 };
	u_int		sx, sy;

	if (client_win32_get_size(&sx, &sy) == 0) {
		if (client_attached &&
		    (sx != client_win32_resize_sx ||
		    sy != client_win32_resize_sy))
			client_win32_send_resize();
		client_win32_resize_sx = sx;
		client_win32_resize_sy = sy;
	}
	evtimer_add(&client_win32_resize_event, &tv);
}

static void
client_win32_start_resize_timer(void)
{
	if (client_flags & CLIENT_CONTROL)
		return;
	if (!client_win32_resize_event_set) {
		evtimer_set(&client_win32_resize_event,
		    client_win32_resize_timer, NULL);
		client_win32_resize_event_set = 1;
	}
	client_win32_resize_timer(-1, 0, NULL);
}
#endif

/* Client main loop. */
int
client_main(struct event_base *base, int argc, char **argv, uint64_t flags,
    int feat)
{
	struct cmd_parse_result	*pr;
	struct msg_command	*data;
	imsg_fd_t		 fd;
	int			 i;
	const char		*ttynam, *termname, *cwd;
#ifndef _WIN32
	pid_t			 ppid;
	struct termios		 tio, saved_tio;
#endif
	enum msgtype		 msg;
	size_t			 size, linesize = 0;
	ssize_t			 linelen;
	char			*line = NULL, **caps = NULL, *cause;
	u_int			 ncaps = 0;
	struct args_value	*values;

	client_base = base;

	/* Set up the initial command. */
	if (shell_command != NULL) {
		msg = MSG_SHELL;
		flags |= CLIENT_STARTSERVER;
	} else if (argc == 0) {
		msg = MSG_COMMAND;
		flags |= CLIENT_STARTSERVER;
	} else {
		msg = MSG_COMMAND;

		/*
		 * It's annoying parsing the command string twice (in client
		 * and later in server) but it is necessary to get the start
		 * server flag.
		 */
		values = args_from_vector(argc, argv);
		pr = cmd_parse_from_arguments(values, argc, NULL);
		if (pr->status == CMD_PARSE_SUCCESS) {
			if (cmd_list_any_have(pr->cmdlist, CMD_STARTSERVER))
				flags |= CLIENT_STARTSERVER;
			cmd_list_free(pr->cmdlist);
		} else
			free(pr->error);
		args_free_values(values, argc);
		free(values);
	}

	/* Create client process structure (starts logging). */
	client_proc = proc_start("client");
#ifdef _WIN32
	client_win32_start_signal_proxy();
#endif
	proc_set_signals(client_proc, client_signal);

	/* Save the flags. */
	client_flags = flags;
	log_debug("flags are %#llx", (unsigned long long)client_flags);

	/* Initialize the client socket and start the server. */
#ifdef HAVE_SYSTEMD
	if (systemd_activated()) {
		/* socket-based activation, do not even try to be a client. */
		fd = server_start(client_proc, flags, base, 0, NULL);
	} else
#endif
	fd = client_connect(base, socket_path, client_flags);
	if (fd == (imsg_fd_t)-1) {
		if (errno == ECONNREFUSED) {
			fprintf(stderr, "no server running on %s\n",
			    socket_path);
		} else {
			fprintf(stderr, "error connecting to %s (%s)\n",
			    socket_path, strerror(errno));
		}
		return (1);
	}
	client_peer = proc_add_peer(client_proc, fd, client_dispatch, NULL);

	/* Save these before pledge(). */
	if ((cwd = find_cwd()) == NULL && (cwd = find_home()) == NULL)
		cwd = find_default_cwd();
#ifdef _WIN32
	ttynam = "";
#else
	if ((ttynam = ttyname(STDIN_FILENO)) == NULL)
		ttynam = "";
#endif
	if ((termname = getenv("TERM")) == NULL)
		termname = "";
#ifdef _WIN32
	if (*termname == '\0')
		termname = "tmux-win32";
#endif

	/*
	 * Drop privileges for client. "proc exec" is needed for -c and for
	 * locking (which uses system(3)).
	 *
	 * "tty" is needed to restore termios(4) and also for some reason -CC
	 * does not work properly without it (input is not recognised).
	 *
	 * "sendfd" is dropped later in client_dispatch_wait().
	 */
	if (pledge(
	    "stdio rpath wpath cpath unix sendfd proc exec tty",
	    NULL) != 0)
		fatal("pledge failed");

	/* Load terminfo entry if any. */
#ifdef _WIN32
	if (*termname != '\0' &&
#else
	if (isatty(STDIN_FILENO) &&
	    *termname != '\0' &&
#endif
	    tty_term_read_list(termname, STDIN_FILENO, &caps, &ncaps,
	    &cause) != 0) {
		fprintf(stderr, "%s\n", cause);
		free(cause);
		return (1);
	}

	/* Free stuff that is not used in the client. */
	if (ptm_fd != -1)
		close(ptm_fd);
	options_free(global_options);
	options_free(global_s_options);
	options_free(global_w_options);
	environ_free(global_environ);

	/* Set up control mode. */
	if (client_flags & CLIENT_CONTROLCONTROL) {
#ifndef _WIN32
		if (tcgetattr(STDIN_FILENO, &saved_tio) != 0) {
			fprintf(stderr, "tcgetattr failed: %s\n",
			    strerror(errno));
			return (1);
		}
		cfmakeraw(&tio);
		tio.c_iflag = ICRNL|IXANY;
		tio.c_oflag = OPOST|ONLCR;
#ifdef NOKERNINFO
		tio.c_lflag = NOKERNINFO;
#endif
		tio.c_cflag = CREAD|CS8|HUPCL;
		tio.c_cc[VMIN] = 1;
		tio.c_cc[VTIME] = 0;
		cfsetispeed(&tio, cfgetispeed(&saved_tio));
		cfsetospeed(&tio, cfgetospeed(&saved_tio));
		tcsetattr(STDIN_FILENO, TCSANOW, &tio);
#endif
	}

	/* Send identify messages. */
	client_send_identify(ttynam, termname, caps, ncaps, cwd, feat);
	tty_term_free_list(caps, ncaps);
	proc_flush_peer(client_peer);

	/* Send first command. */
	if (msg == MSG_COMMAND) {
		/* How big is the command? */
		size = 0;
		for (i = 0; i < argc; i++)
			size += strlen(argv[i]) + 1;
		if (size > MAX_IMSGSIZE - (sizeof *data)) {
			fprintf(stderr, "command too long\n");
			return (1);
		}
		data = xmalloc((sizeof *data) + size);

		/* Prepare command for server. */
		data->argc = argc;
		if (cmd_pack_argv(argc, argv, (char *)(data + 1), size) != 0) {
			fprintf(stderr, "command too long\n");
			free(data);
			return (1);
		}
		size += sizeof *data;

		/* Send the command. */
		if (proc_send(client_peer, msg, -1, data, size) != 0) {
			fprintf(stderr, "failed to send command\n");
			free(data);
			return (1);
		}
		free(data);
	} else if (msg == MSG_SHELL)
		proc_send(client_peer, msg, -1, NULL, 0);

#ifdef _WIN32
	client_win32_start_resize_timer();
#endif

	/* Start main loop. */
	proc_loop(client_proc, NULL);

#ifdef _WIN32
	client_win32_stop_stdin_proxy();
	client_win32_stop_signal_proxy();
	client_win32_stop_stdout_proxy();
#endif

	/* Run command if user requested exec, instead of exiting. */
	if (client_exittype == MSG_EXEC) {
#ifndef _WIN32
		if (client_flags & CLIENT_CONTROLCONTROL)
			tcsetattr(STDOUT_FILENO, TCSAFLUSH, &saved_tio);
#endif
		client_exec(client_execshell, client_execcmd);
	}

	/* Restore streams to blocking. */
#ifndef _WIN32
	setblocking(STDIN_FILENO, 1);
	setblocking(STDOUT_FILENO, 1);
	setblocking(STDERR_FILENO, 1);
#endif

	/* Print the exit message, if any, and exit. */
	if (client_attached) {
		if (client_exitreason != CLIENT_EXIT_NONE)
			printf("[%s]\n", client_exit_message());

#ifndef _WIN32
		ppid = getppid();
		if (client_exittype == MSG_DETACHKILL && ppid > 1)
			kill(ppid, SIGHUP);
#endif
	} else if (client_flags & CLIENT_CONTROL) {
		if (client_exitreason != CLIENT_EXIT_NONE)
			printf("%%exit %s\n", client_exit_message());
		else
			printf("%%exit\n");
		fflush(stdout);
		if (client_flags & CLIENT_CONTROL_WAITEXIT) {
			setvbuf(stdin, NULL, _IOLBF, 0);
			for (;;) {
				linelen = getline(&line, &linesize, stdin);
				if (linelen <= 1)
					break;
			}
			free(line);
		}
		if (client_flags & CLIENT_CONTROLCONTROL) {
			printf("\033\\");
			fflush(stdout);
#ifndef _WIN32
			tcsetattr(STDOUT_FILENO, TCSAFLUSH, &saved_tio);
#endif
		}
	} else if (client_exitreason != CLIENT_EXIT_NONE)
		fprintf(stderr, "%s\n", client_exit_message());
	return (client_exitval);
}

/* Send identify messages to server. */
static void
client_send_identify(const char *ttynam, const char *termname, char **caps,
    u_int ncaps, const char *cwd, int feat)
{
	char	**ss;
	size_t	  sslen;
#ifndef _WIN32
	int	  fd;
#else
	struct win32_handle_message handle;
#endif
	uint64_t  flags = client_flags;
	pid_t	  pid;
	u_int	  i;

	proc_send(client_peer, MSG_IDENTIFY_LONGFLAGS, -1, &flags, sizeof flags);
	proc_send(client_peer, MSG_IDENTIFY_LONGFLAGS, -1, &client_flags,
	    sizeof client_flags);

	proc_send(client_peer, MSG_IDENTIFY_TERM, -1, termname,
	    strlen(termname) + 1);
	proc_send(client_peer, MSG_IDENTIFY_FEATURES, -1, &feat, sizeof feat);

	proc_send(client_peer, MSG_IDENTIFY_TTYNAME, -1, ttynam,
	    strlen(ttynam) + 1);
	proc_send(client_peer, MSG_IDENTIFY_CWD, -1, cwd, strlen(cwd) + 1);

	for (i = 0; i < ncaps; i++) {
		proc_send(client_peer, MSG_IDENTIFY_TERMINFO, -1,
		    caps[i], strlen(caps[i]) + 1);
	}

	pid = getpid();
	proc_send(client_peer, MSG_IDENTIFY_CLIENTPID, -1, &pid, sizeof pid);

#ifdef _WIN32
	if (win32_handle_message_from_fd(STDIN_FILENO, &handle) == 0)
		proc_send(client_peer, MSG_IDENTIFY_STDIN, -1, &handle,
		    sizeof handle);
	else
		proc_send(client_peer, MSG_IDENTIFY_STDIN, -1, NULL, 0);
	if (client_win32_start_stdout_proxy(&handle) == 0)
		proc_send(client_peer, MSG_IDENTIFY_STDOUT, -1, &handle,
		    sizeof handle);
	else if (win32_handle_message_from_fd(STDOUT_FILENO, &handle) == 0)
		proc_send(client_peer, MSG_IDENTIFY_STDOUT, -1, &handle,
		    sizeof handle);
	else
		proc_send(client_peer, MSG_IDENTIFY_STDOUT, -1, NULL, 0);
#else
	if ((fd = dup(STDIN_FILENO)) == -1)
		fatal("dup failed");
	proc_send(client_peer, MSG_IDENTIFY_STDIN, fd, NULL, 0);
	if ((fd = dup(STDOUT_FILENO)) == -1)
		fatal("dup failed");
	proc_send(client_peer, MSG_IDENTIFY_STDOUT, fd, NULL, 0);
#endif

	for (ss = TMUX_ENVIRON; *ss != NULL; ss++) {
		sslen = strlen(*ss) + 1;
		if (sslen > MAX_IMSGSIZE - IMSG_HEADER_SIZE)
			continue;
		proc_send(client_peer, MSG_IDENTIFY_ENVIRON, -1, *ss, sslen);
	}

	proc_send(client_peer, MSG_IDENTIFY_DONE, -1, NULL, 0);
}

/* Run command in shell; used for -c. */
static __dead void
client_exec(const char *shell, const char *shellcmd)
{
	char	*argv0;

	log_debug("shell %s, command %s", shell, shellcmd);
	argv0 = shell_argv0(shell, !!(client_flags & CLIENT_LOGIN));

#ifdef _WIN32
	_putenv_s("SHELL", shell);
	proc_clear_signals(client_proc, 1);

	if (win32_shell_is_cmd(shell))
		_spawnl(_P_OVERLAY, shell, argv0, "/d", "/c", shellcmd,
		    (char *)NULL);
	else
		_spawnl(_P_OVERLAY, shell, argv0, "-c", shellcmd,
		    (char *)NULL);
	fatal("spawn failed");
#else
	setenv("SHELL", shell, 1);

	proc_clear_signals(client_proc, 1);

	setblocking(STDIN_FILENO, 1);
	setblocking(STDOUT_FILENO, 1);
	setblocking(STDERR_FILENO, 1);
	closefrom(STDERR_FILENO + 1);

	execl(shell, argv0, "-c", shellcmd, (char *) NULL);
	fatal("execl failed");
#endif
}

/* Callback to handle signals in the client. */
static void
client_signal(int sig)
{
#ifdef _WIN32
	if (sig == SIGINT && client_attached)
		client_win32_send_signal_input('\003');
	else if (!client_attached)
		proc_exit(client_proc);
#else
	struct sigaction sigact;
	int		 status;
	pid_t		 pid;

	log_debug("%s: %s", __func__, strsignal(sig));
	if (sig == SIGCHLD) {
		for (;;) {
			pid = waitpid(WAIT_ANY, &status, WNOHANG);
			if (pid == 0)
				break;
			if (pid == -1) {
				if (errno == ECHILD)
					break;
				log_debug("waitpid failed: %s",
				    strerror(errno));
			}
		}
	} else if (!client_attached) {
		if (sig == SIGTERM || sig == SIGHUP)
			proc_exit(client_proc);
	} else {
		switch (sig) {
		case SIGHUP:
			client_exitreason = CLIENT_EXIT_LOST_TTY;
			client_exitval = 1;
			proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
			break;
		case SIGTERM:
			if (!client_suspended)
				client_exitreason = CLIENT_EXIT_TERMINATED;
			client_exitval = 1;
			proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
			break;
		case SIGWINCH:
			proc_send(client_peer, MSG_RESIZE, -1, NULL, 0);
			break;
		case SIGCONT:
			memset(&sigact, 0, sizeof sigact);
			sigemptyset(&sigact.sa_mask);
			sigact.sa_flags = SA_RESTART;
			sigact.sa_handler = SIG_IGN;
			if (sigaction(SIGTSTP, &sigact, NULL) != 0)
				fatal("sigaction failed");
			proc_send(client_peer, MSG_WAKEUP, -1, NULL, 0);
			client_suspended = 0;
			break;
		}
	}
#endif
}

/* Callback for file write error or close. */
static void
client_file_check_cb(__unused struct client *c, __unused const char *path,
    __unused int error, __unused int closed, __unused struct evbuffer *buffer,
    __unused void *data)
{
	if (client_exitflag)
		client_exit();
}

/* Callback for client read events. */
static void
client_dispatch(struct imsg *imsg, __unused void *arg)
{
	if (imsg == NULL) {
		if (!client_exitflag) {
			client_exitreason = CLIENT_EXIT_LOST_SERVER;
			client_exitval = 1;
		}
		proc_exit(client_proc);
		return;
	}

	if (client_attached)
		client_dispatch_attached(imsg);
	else
		client_dispatch_wait(imsg);
}

/* Process an exit message. */
static void
client_dispatch_exit_message(char *data, size_t datalen)
{
	int	retval;

	if (datalen < sizeof retval && datalen != 0)
		fatalx("bad MSG_EXIT size");

	if (datalen >= sizeof retval) {
		memcpy(&retval, data, sizeof retval);
		client_exitval = retval;
	}

	if (datalen > sizeof retval) {
		datalen -= sizeof retval;
		data += sizeof retval;

		client_exitmessage = xmalloc(datalen);
		memcpy(client_exitmessage, data, datalen);
		client_exitmessage[datalen - 1] = '\0';

		client_exitreason = CLIENT_EXIT_MESSAGE_PROVIDED;
	}
}

/* Dispatch imsgs when in wait state (before MSG_READY). */
static void
client_dispatch_wait(struct imsg *imsg)
{
	char		*data;
	ssize_t		 datalen;
	static int	 pledge_applied;

	/*
	 * "sendfd" is no longer required once all of the identify messages
	 * have been sent. We know the server won't send us anything until that
	 * point (because we don't ask it to), so we can drop "sendfd" once we
	 * get the first message from the server.
	 */
	if (!pledge_applied) {
		if (pledge(
		    "stdio rpath wpath cpath unix proc exec tty",
		    NULL) != 0)
			fatal("pledge failed");
		pledge_applied = 1;
	}

	data = imsg->data;
	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;

	switch (imsg->hdr.type) {
	case MSG_EXIT:
	case MSG_SHUTDOWN:
		client_dispatch_exit_message(data, datalen);
		client_exitflag = 1;
		client_exit();
		break;
	case MSG_READY:
		if (datalen != 0)
			fatalx("bad MSG_READY size");

		client_attached = 1;
#ifdef _WIN32
		client_win32_start_stdin_proxy();
		client_win32_send_resize();
#else
		proc_send(client_peer, MSG_RESIZE, -1, NULL, 0);
#endif
		break;
	case MSG_VERSION:
		if (datalen != 0)
			fatalx("bad MSG_VERSION size");

		fprintf(stderr, "protocol version mismatch "
		    "(client %d, server %u)\n", PROTOCOL_VERSION,
		    imsg->hdr.peerid & 0xff);
		client_exitval = 1;
		proc_exit(client_proc);
		break;
	case MSG_FLAGS:
		if (datalen != sizeof client_flags)
			fatalx("bad MSG_FLAGS string");

		memcpy(&client_flags, data, sizeof client_flags);
		log_debug("new flags are %#llx",
		    (unsigned long long)client_flags);
		break;
	case MSG_SHELL:
		if (datalen == 0 || data[datalen - 1] != '\0')
			fatalx("bad MSG_SHELL string");

		client_exec(data, shell_command);
		/* NOTREACHED */
	case MSG_DETACH:
	case MSG_DETACHKILL:
		proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		break;
	case MSG_EXITED:
		proc_exit(client_proc);
		break;
	case MSG_READ_OPEN:
		file_read_open(&client_files, client_peer, imsg, 1,
		    !(client_flags & CLIENT_CONTROL), client_file_check_cb,
		    NULL);
		break;
	case MSG_READ_CANCEL:
		file_read_cancel(&client_files, imsg);
		break;
	case MSG_WRITE_OPEN:
		file_write_open(&client_files, client_peer, imsg, 1,
		    !(client_flags & CLIENT_CONTROL), client_file_check_cb,
		    NULL);
		break;
	case MSG_WRITE:
		file_write_data(&client_files, imsg);
		break;
	case MSG_WRITE_CLOSE:
		file_write_close(&client_files, imsg);
		break;
	case MSG_OLDSTDERR:
	case MSG_OLDSTDIN:
	case MSG_OLDSTDOUT:
		fprintf(stderr, "server version is too old for client\n");
		proc_exit(client_proc);
		break;
	}
}

/* Dispatch imsgs in attached state (after MSG_READY). */
static void
client_dispatch_attached(struct imsg *imsg)
{
#ifndef _WIN32
	struct sigaction	 sigact;
#endif
	char			*data;
	ssize_t			 datalen;

	data = imsg->data;
	datalen = imsg->hdr.len - IMSG_HEADER_SIZE;

	switch (imsg->hdr.type) {
	case MSG_FLAGS:
		if (datalen != sizeof client_flags)
			fatalx("bad MSG_FLAGS string");

		memcpy(&client_flags, data, sizeof client_flags);
		log_debug("new flags are %#llx",
		    (unsigned long long)client_flags);
		break;
	case MSG_DETACH:
	case MSG_DETACHKILL:
		if (datalen == 0 || data[datalen - 1] != '\0')
			fatalx("bad MSG_DETACH string");

		client_exitsession = xstrdup(data);
		client_exittype = imsg->hdr.type;
		if (imsg->hdr.type == MSG_DETACHKILL)
			client_exitreason = CLIENT_EXIT_DETACHED_HUP;
		else
			client_exitreason = CLIENT_EXIT_DETACHED;
		proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		break;
	case MSG_EXEC:
		if (datalen == 0 || data[datalen - 1] != '\0' ||
		    strlen(data) + 1 == (size_t)datalen)
			fatalx("bad MSG_EXEC string");
		client_execcmd = xstrdup(data);
		client_execshell = xstrdup(data + strlen(data) + 1);

		client_exittype = imsg->hdr.type;
		proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		break;
	case MSG_EXIT:
		client_dispatch_exit_message(data, datalen);
		if (client_exitreason == CLIENT_EXIT_NONE)
			client_exitreason = CLIENT_EXIT_EXITED;
		proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		break;
	case MSG_EXITED:
		if (datalen != 0)
			fatalx("bad MSG_EXITED size");

		proc_exit(client_proc);
		break;
	case MSG_SHUTDOWN:
		if (datalen != 0)
			fatalx("bad MSG_SHUTDOWN size");

		proc_send(client_peer, MSG_EXITING, -1, NULL, 0);
		client_exitreason = CLIENT_EXIT_SERVER_EXITED;
		client_exitval = 1;
		break;
	case MSG_SUSPEND:
		if (datalen != 0)
			fatalx("bad MSG_SUSPEND size");

#ifdef _WIN32
		break;
#else
		memset(&sigact, 0, sizeof sigact);
		sigemptyset(&sigact.sa_mask);
		sigact.sa_flags = SA_RESTART;
		sigact.sa_handler = SIG_DFL;
		if (sigaction(SIGTSTP, &sigact, NULL) != 0)
			fatal("sigaction failed");
		client_suspended = 1;
		kill(getpid(), SIGTSTP);
		break;
#endif
	case MSG_LOCK:
		if (datalen == 0 || data[datalen - 1] != '\0')
			fatalx("bad MSG_LOCK string");

		system(data);
		proc_send(client_peer, MSG_UNLOCK, -1, NULL, 0);
		break;
	}
}
