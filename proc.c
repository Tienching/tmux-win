/* $OpenBSD$ */

/*
 * Copyright (c) 2015 Nicholas Marriott <nicholas.marriott@gmail.com>
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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sys/types.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#endif

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#if defined(HAVE_NCURSES_H)
#include <ncurses.h>
#endif

#include "tmux.h"

#ifdef _WIN32
#include "compat/win32-socketpair.h"
#endif

struct tmuxproc {
	const char	 *name;
	int		  exit;

	void		(*signalcb)(int);

	struct event	  ev_sigint;
	struct event	  ev_sighup;
	struct event	  ev_sigchld;
	struct event	  ev_sigcont;
	struct event	  ev_sigterm;
	struct event	  ev_sigusr1;
	struct event	  ev_sigusr2;
	struct event	  ev_sigwinch;

	TAILQ_HEAD(, tmuxpeer) peers;
};

struct tmuxpeer {
	struct tmuxproc	*parent;

	struct imsgbuf	 ibuf;
	struct event	 event;
#ifdef _WIN32
	struct event	 poll_event;
#endif
	uid_t		 uid;

	int		 flags;
#define PEER_BAD 0x1
#define PEER_IN_CALLBACK 0x2

	void		(*dispatchcb)(struct imsg *, void *);
	void		 *arg;

	TAILQ_ENTRY(tmuxpeer) entry;
};

static int	peer_check_version(struct tmuxpeer *, struct imsg *);
static void	proc_update_event(struct tmuxpeer *);
#ifdef _WIN32
static void	proc_win32_poll_cb(evutil_socket_t, short, void *);
#endif

#ifdef _WIN32
static struct tmuxproc	*proc_win32_signal_proc;

static BOOL WINAPI
proc_win32_console_handler(DWORD type)
{
	int	signo;

	if (proc_win32_signal_proc == NULL ||
	    proc_win32_signal_proc->signalcb == NULL)
		return (FALSE);

	switch (type) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		signo = SIGINT;
		break;
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		signo = SIGTERM;
		break;
	default:
		return (FALSE);
	}
	proc_win32_signal_proc->signalcb(signo);
	return (TRUE);
}
#endif

static void
proc_event_cb(__unused evutil_socket_t fd, short events, void *arg)
{
	struct tmuxpeer	*peer = arg;
	ssize_t		 n;
	struct imsg	 imsg;
	int		 want_write;

	if (!(peer->flags & PEER_BAD) && (events & EV_READ)) {
		if (imsgbuf_read(&peer->ibuf) != 1) {
			peer->dispatchcb(NULL, peer->arg);
			return;
		}
		for (;;) {
			if ((n = imsg_get(&peer->ibuf, &imsg)) == -1) {
				peer->dispatchcb(NULL, peer->arg);
				return;
			}
			if (n == 0)
				break;
			log_debug("peer %p message %d", peer, imsg.hdr.type);

			if (peer_check_version(peer, &imsg) != 0) {
				imsg_free(&imsg);
				break;
			}

			peer->flags |= PEER_IN_CALLBACK;
			peer->dispatchcb(&imsg, peer->arg);
			peer->flags &= ~PEER_IN_CALLBACK;
			imsg_free(&imsg);
		}
	}

	want_write = (events & EV_WRITE);
#ifdef _WIN32
	if (imsgbuf_queuelen(&peer->ibuf) > 0)
		want_write = 1;
#endif

	if (want_write) {
		if (imsgbuf_write(&peer->ibuf) == -1) {
			peer->dispatchcb(NULL, peer->arg);
			return;
		}
	}

	if ((peer->flags & PEER_BAD) && imsgbuf_queuelen(&peer->ibuf) == 0) {
		peer->dispatchcb(NULL, peer->arg);
		return;
	}

	proc_update_event(peer);
}

#ifdef _WIN32
static void
proc_win32_poll_cb(__unused evutil_socket_t fd, __unused short events, void *arg)
{
	struct tmuxpeer	*peer = arg;
	struct timeval	 tv = { .tv_sec = 0, .tv_usec = 10000 };
	unsigned long	 pending;
	short		 ready = 0;

	/*
	 * The libevent win32 backend can miss socket readiness after a peer
	 * queues a reply from inside a read callback. Poll nonblocking sockets
	 * on a short persistent timer so IPC makes progress.
	 */
	evtimer_add(&peer->poll_event, &tv);
	if (win32_socket_pending(peer->ibuf.fd, &pending) == 0 && pending != 0)
		ready |= EV_READ;
	if (imsgbuf_queuelen(&peer->ibuf) > 0)
		ready |= EV_WRITE;
	if (ready != 0)
		proc_event_cb(peer->ibuf.fd, ready, peer);
}
#endif

#ifndef _WIN32
static void
proc_signal_cb(evutil_socket_t signo, __unused short events, void *arg)
{
	struct tmuxproc	*tp = arg;

	tp->signalcb(signo);
}
#endif

static int
peer_check_version(struct tmuxpeer *peer, struct imsg *imsg)
{
	int	version;

	version = imsg->hdr.peerid & 0xff;
	if (imsg->hdr.type != MSG_VERSION && version != PROTOCOL_VERSION) {
		log_debug("peer %p bad version %d", peer, version);

		proc_send(peer, MSG_VERSION, -1, NULL, 0);
		peer->flags |= PEER_BAD;

		return (-1);
	}
	return (0);
}

static void
proc_update_event(struct tmuxpeer *peer)
{
	short	events;

	event_del(&peer->event);

	events = EV_READ;
	if (imsgbuf_queuelen(&peer->ibuf) > 0)
		events |= EV_WRITE;
	event_set(&peer->event, peer->ibuf.fd, events, proc_event_cb, peer);

	event_add(&peer->event, NULL);
}

int
proc_send(struct tmuxpeer *peer, enum msgtype type, int fd, const void *buf,
    size_t len)
{
	struct imsgbuf	*ibuf = &peer->ibuf;
	void		*vp = (void *)buf;
	int		 retval;

	if (peer->flags & PEER_BAD)
		return (-1);
	log_debug("sending message %d to peer %p (%zu bytes)", type, peer, len);

	retval = imsg_compose(ibuf, type, PROTOCOL_VERSION, -1, fd, vp, len);
	if (retval != 1)
		return (-1);
	if (~peer->flags & PEER_IN_CALLBACK)
		proc_update_event(peer);
	return (0);
}

struct tmuxproc *
proc_start(const char *name)
{
	struct tmuxproc	*tp;
#ifndef _WIN32
	struct utsname	 u;
#endif

	log_open(name);
	setproctitle("%s (%s)", name, socket_path);

#ifdef _WIN32
	log_debug("%s started (%ld): version %s, socket %s, protocol %d", name,
	    (long)GetCurrentProcessId(), getversion(), socket_path,
	    PROTOCOL_VERSION);
	log_debug("on Windows");
#else
	if (uname(&u) < 0)
		memset(&u, 0, sizeof u);

	log_debug("%s started (%ld): version %s, socket %s, protocol %d", name,
	    (long)getpid(), getversion(), socket_path, PROTOCOL_VERSION);
	log_debug("on %s %s %s", u.sysname, u.release, u.version);
#endif
	log_debug("using libevent %s %s", event_get_version(), event_get_method());
#ifdef HAVE_UTF8PROC
	log_debug("using utf8proc %s", utf8proc_version());
#endif
#ifdef NCURSES_VERSION
	log_debug("using ncurses %s %06u", NCURSES_VERSION, NCURSES_VERSION_PATCH);
#endif

	tp = xcalloc(1, sizeof *tp);
	tp->name = xstrdup(name);
	TAILQ_INIT(&tp->peers);

	return (tp);
}

void
proc_loop(struct tmuxproc *tp, int (*loopcb)(void))
{
	log_debug("%s loop enter", tp->name);
	do
		event_loop(EVLOOP_ONCE);
	while (!tp->exit && (loopcb == NULL || !loopcb ()));
	log_debug("%s loop exit", tp->name);
}

void
proc_exit(struct tmuxproc *tp)
{
	struct tmuxpeer	*peer;

	TAILQ_FOREACH(peer, &tp->peers, entry)
	    imsgbuf_flush(&peer->ibuf);
	tp->exit = 1;
}

void
proc_set_signals(struct tmuxproc *tp, void (*signalcb)(int))
{
#ifdef _WIN32
	tp->signalcb = signalcb;
	proc_win32_signal_proc = tp;
	SetConsoleCtrlHandler(proc_win32_console_handler, TRUE);
#else
	struct sigaction	sa;

	tp->signalcb = signalcb;

	memset(&sa, 0, sizeof sa);
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_IGN;

	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGTTIN, &sa, NULL);
	sigaction(SIGTTOU, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);

	signal_set(&tp->ev_sigint, SIGINT, proc_signal_cb, tp);
	signal_add(&tp->ev_sigint, NULL);
	signal_set(&tp->ev_sighup, SIGHUP, proc_signal_cb, tp);
	signal_add(&tp->ev_sighup, NULL);
	signal_set(&tp->ev_sigchld, SIGCHLD, proc_signal_cb, tp);
	signal_add(&tp->ev_sigchld, NULL);
	signal_set(&tp->ev_sigcont, SIGCONT, proc_signal_cb, tp);
	signal_add(&tp->ev_sigcont, NULL);
	signal_set(&tp->ev_sigterm, SIGTERM, proc_signal_cb, tp);
	signal_add(&tp->ev_sigterm, NULL);
	signal_set(&tp->ev_sigusr1, SIGUSR1, proc_signal_cb, tp);
	signal_add(&tp->ev_sigusr1, NULL);
	signal_set(&tp->ev_sigusr2, SIGUSR2, proc_signal_cb, tp);
	signal_add(&tp->ev_sigusr2, NULL);
	signal_set(&tp->ev_sigwinch, SIGWINCH, proc_signal_cb, tp);
	signal_add(&tp->ev_sigwinch, NULL);
#endif
}

void
proc_clear_signals(struct tmuxproc *tp, int defaults)
{
#ifdef _WIN32
	(void)defaults;
	if (proc_win32_signal_proc == tp) {
		SetConsoleCtrlHandler(proc_win32_console_handler, FALSE);
		proc_win32_signal_proc = NULL;
	}
#else
	struct sigaction	sa;

	memset(&sa, 0, sizeof sa);
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = SIG_DFL;

	sigaction(SIGPIPE, &sa, NULL);
	sigaction(SIGTSTP, &sa, NULL);

	signal_del(&tp->ev_sigint);
	signal_del(&tp->ev_sighup);
	signal_del(&tp->ev_sigchld);
	signal_del(&tp->ev_sigcont);
	signal_del(&tp->ev_sigterm);
	signal_del(&tp->ev_sigusr1);
	signal_del(&tp->ev_sigusr2);
	signal_del(&tp->ev_sigwinch);

	if (defaults) {
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGQUIT, &sa, NULL);
		sigaction(SIGHUP, &sa, NULL);
		sigaction(SIGCHLD, &sa, NULL);
		sigaction(SIGCONT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
		sigaction(SIGUSR1, &sa, NULL);
		sigaction(SIGUSR2, &sa, NULL);
		sigaction(SIGWINCH, &sa, NULL);
	}
#endif
}

struct tmuxpeer *
proc_add_peer(struct tmuxproc *tp, imsg_fd_t fd,
    void (*dispatchcb)(struct imsg *, void *), void *arg)
{
	struct tmuxpeer	*peer;
#ifdef _WIN32
	struct timeval	 tv = { .tv_sec = 0, .tv_usec = 10000 };
#else
	gid_t		 gid;
#endif

	peer = xcalloc(1, sizeof *peer);
	peer->parent = tp;

	peer->dispatchcb = dispatchcb;
	peer->arg = arg;

	if (imsgbuf_init(&peer->ibuf, fd) == -1)
		fatal("imsgbuf_init");
#ifndef _WIN32
	imsgbuf_allow_fdpass(&peer->ibuf);
#endif
	event_set(&peer->event, fd, EV_READ, proc_event_cb, peer);

#ifdef _WIN32
	peer->uid = TMUX_WIN32_OWNER_UID;
#else
	if (getpeereid(fd, &peer->uid, &gid) != 0)
		peer->uid = (uid_t)-1;
#endif

	log_debug("add peer %p: %llu (%p)", peer,
	    (unsigned long long)fd, arg);
	TAILQ_INSERT_TAIL(&tp->peers, peer, entry);

	proc_update_event(peer);
#ifdef _WIN32
	evtimer_set(&peer->poll_event, proc_win32_poll_cb, peer);
	event_add(&peer->poll_event, &tv);
#endif
	return (peer);
}

void
proc_remove_peer(struct tmuxpeer *peer)
{
	TAILQ_REMOVE(&peer->parent->peers, peer, entry);
	log_debug("remove peer %p", peer);

	event_del(&peer->event);
#ifdef _WIN32
	event_del(&peer->poll_event);
#endif
	imsgbuf_clear(&peer->ibuf);

#ifdef _WIN32
	win32_socket_close(peer->ibuf.fd);
#else
	close(peer->ibuf.fd);
#endif
	free(peer);
}

void
proc_kill_peer(struct tmuxpeer *peer)
{
	peer->flags |= PEER_BAD;
}

void
proc_flush_peer(struct tmuxpeer *peer)
{
	imsgbuf_flush(&peer->ibuf);
}

void
proc_toggle_log(struct tmuxproc *tp)
{
	log_toggle(tp->name);
}

pid_t
proc_fork_and_daemon(int *fd)
{
#ifdef _WIN32
	(void)fd;
	fatalx("proc_fork_and_daemon is not implemented on Windows");
#else
	pid_t	pid;
	int	pair[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, pair) != 0)
		fatal("socketpair failed");
	switch (pid = fork()) {
	case -1:
		fatal("fork failed");
	case 0:
		close(pair[0]);
		*fd = pair[1];
		if (daemon(1, 0) != 0)
			fatal("daemon failed");
		return (0);
	default:
		close(pair[1]);
		*fd = pair[0];
		return (pid);
	}
#endif
}

uid_t
proc_get_peer_uid(struct tmuxpeer *peer)
{
	return (peer->uid);
}
