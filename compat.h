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

#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#endif

#ifndef _WIN32
#include <fnmatch.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <termios.h>
#endif
#include <wchar.h>

#ifdef _WIN32
typedef uint32_t utf8_wchar;
#else
typedef wchar_t utf8_wchar;
#endif

#ifdef _WIN32
#define TMUX_ENVIRON _environ
#ifdef environ
#undef environ
#endif
#ifndef _TMUX_WIN32_UID_T_DEFINED
#define _TMUX_WIN32_UID_T_DEFINED
typedef int uid_t;
typedef int gid_t;
#endif
#ifndef _TMUX_WIN32_CC_T_DEFINED
#define _TMUX_WIN32_CC_T_DEFINED
typedef unsigned char cc_t;
#endif
#ifndef SIGHUP
#define SIGHUP 1
#endif
#else
#define TMUX_ENVIRON environ
#endif

#ifdef _WIN32
#ifndef FNM_NOMATCH
#define FNM_NOMATCH 1
#endif
#ifndef FNM_NOESCAPE
#define FNM_NOESCAPE 0x01
#endif
#ifndef FNM_PATHNAME
#define FNM_PATHNAME 0x02
#endif
#ifndef FNM_PERIOD
#define FNM_PERIOD 0x04
#endif
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0x08
#endif
int	fnmatch(const char *, const char *, int);
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#ifndef S_IRWXU
#define S_IRWXU (_S_IREAD|_S_IWRITE)
#endif
#ifndef S_IRWXG
#define S_IRWXG 0
#endif
#ifndef S_IRWXO
#define S_IRWXO 0
#endif
#ifndef S_IRUSR
#define S_IRUSR _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR _S_IWRITE
#endif
#ifndef S_IXUSR
#define S_IXUSR 0
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IWGRP
#define S_IWGRP 0
#endif
#ifndef S_IXGRP
#define S_IXGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IWOTH
#define S_IWOTH 0
#endif
#ifndef S_IXOTH
#define S_IXOTH 0
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef TMUX_WIN32_OWNER_UID
#define TMUX_WIN32_OWNER_UID 1
#endif
#ifndef WIFEXITED
#define WIFEXITED(status) (((status) & 0x7f) == 0)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && \
	((status) & 0x7f) != 0x7f)
#endif
#ifndef WTERMSIG
#define WTERMSIG(status) ((status) & 0x7f)
#endif
#ifndef _TMUX_WIN32_TERMIOS_DEFINED
#define _TMUX_WIN32_TERMIOS_DEFINED
#ifndef NCCS
#define NCCS 32
#endif
struct termios {
	unsigned int	c_iflag;
	unsigned int	c_oflag;
	unsigned int	c_cflag;
	unsigned int	c_lflag;
	unsigned char	c_cc[NCCS];
};
struct winsize {
	unsigned short	ws_row;
	unsigned short	ws_col;
	unsigned short	ws_xpixel;
	unsigned short	ws_ypixel;
};
#endif
#ifndef TCSANOW
#define TCSANOW 0
#endif
#ifndef TCSAFLUSH
#define TCSAFLUSH 2
#endif
#ifndef VMIN
#define VMIN 16
#endif
#ifndef VTIME
#define VTIME 17
#endif
#ifndef VERASE
#define VERASE 2
#endif
#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE 0xff
#endif
#ifndef ICRNL
#define ICRNL 0x0001
#endif
#ifndef IXANY
#define IXANY 0x0002
#endif
#ifndef IXON
#define IXON 0x0004
#endif
#ifndef IXOFF
#define IXOFF 0x0008
#endif
#ifndef INLCR
#define INLCR 0x0010
#endif
#ifndef IGNCR
#define IGNCR 0x0020
#endif
#ifndef IGNBRK
#define IGNBRK 0x0040
#endif
#ifndef ISTRIP
#define ISTRIP 0x0080
#endif
#ifndef IUTF8
#define IUTF8 0x0100
#endif
#ifndef OPOST
#define OPOST 0x0001
#endif
#ifndef ONLCR
#define ONLCR 0x0002
#endif
#ifndef OCRNL
#define OCRNL 0x0004
#endif
#ifndef ONLRET
#define ONLRET 0x0008
#endif
#ifndef CREAD
#define CREAD 0x0001
#endif
#ifndef CS8
#define CS8 0x0002
#endif
#ifndef HUPCL
#define HUPCL 0x0004
#endif
#ifndef IEXTEN
#define IEXTEN 0x0001
#endif
#ifndef ICANON
#define ICANON 0x0002
#endif
#ifndef ECHO
#define ECHO 0x0004
#endif
#ifndef ECHOE
#define ECHOE 0x0008
#endif
#ifndef ECHONL
#define ECHONL 0x0010
#endif
#ifndef ECHOCTL
#define ECHOCTL 0x0020
#endif
#ifndef ISIG
#define ISIG 0x0040
#endif
#ifndef NOFLSH
#define NOFLSH 0x0080
#endif
#ifndef NOKERNINFO
#define NOKERNINFO 0
#endif
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0
#endif
#ifndef TIOCSWINSZ
#define TIOCSWINSZ 0
#endif
#endif

#ifdef HAVE_EVENT2_EVENT_H
#include <event2/event.h>
#include <event2/event_compat.h>
#include <event2/event_struct.h>
#include <event2/buffer.h>
#include <event2/buffer_compat.h>
#include <event2/bufferevent.h>
#include <event2/bufferevent_struct.h>
#include <event2/bufferevent_compat.h>
#else
#include <event.h>
#ifndef EVBUFFER_EOL_LF
/*
 * This doesn't really work because evbuffer_readline is broken, but gets us to
 * build with very old (older than 1.4.14) libevent.
 */
#define EVBUFFER_EOL_LF
#define evbuffer_readln(a, b, c) evbuffer_readline(a)
#endif
#endif

#ifdef HAVE_MALLOC_TRIM
#include <malloc.h>
#endif

#ifdef HAVE_UTF8PROC
#include <utf8proc.h>
#endif

#ifndef __GNUC__
#define __attribute__(a)
#endif

#ifdef BROKEN___DEAD
#undef __dead
#endif

#ifndef __unused
#define __unused __attribute__ ((__unused__))
#endif
#ifndef __dead
#define __dead __attribute__ ((__noreturn__))
#endif
#ifndef __packed
#define __packed __attribute__ ((__packed__))
#endif
#ifndef __weak
#define __weak __attribute__ ((__weak__))
#endif

#ifndef ECHOPRT
#define ECHOPRT 0
#endif

#ifndef ACCESSPERMS
#define ACCESSPERMS (S_IRWXU|S_IRWXG|S_IRWXO)
#endif

#if !defined(FIONREAD) && defined(__sun)
#include <sys/filio.h>
#endif

#ifdef HAVE_ERR_H
#include <err.h>
#else
void	err(int, const char *, ...);
void	errx(int, const char *, ...);
void	warn(const char *, ...);
void	warnx(const char *, ...);
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifndef _PATH_BSHELL
#ifdef _WIN32
#define _PATH_BSHELL	"cmd.exe"
#else
#define _PATH_BSHELL	"/bin/sh"
#endif
#endif

#ifndef _PATH_TMP
#ifdef _WIN32
#define _PATH_TMP	".\\"
#else
#define _PATH_TMP	"/tmp/"
#endif
#endif

#ifndef _PATH_DEVNULL
#ifdef _WIN32
#define _PATH_DEVNULL	"NUL"
#else
#define _PATH_DEVNULL	"/dev/null"
#endif
#endif

#ifndef _PATH_TTY
#ifdef _WIN32
#define _PATH_TTY	"CON"
#else
#define _PATH_TTY	"/dev/tty"
#endif
#endif

#ifndef _PATH_DEV
#ifdef _WIN32
#define _PATH_DEV	""
#else
#define _PATH_DEV	"/dev/"
#endif
#endif

#ifndef _PATH_DEFPATH
#ifdef _WIN32
#define _PATH_DEFPATH	"C:\\Windows\\System32;C:\\Windows"
#else
#define _PATH_DEFPATH	"/usr/bin:/bin"
#endif
#endif

#ifndef _PATH_VI
#ifdef _WIN32
#define _PATH_VI	"notepad.exe"
#else
#define _PATH_VI	"/usr/bin/vi"
#endif
#endif

#ifndef __OpenBSD__
#define pledge(s, p) (0)
#endif

#ifndef IMAXBEL
#define IMAXBEL 0
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
#include <inttypes.h>
#endif

#ifdef HAVE_QUEUE_H
#include <sys/queue.h>
#else
#include "compat/queue.h"
#endif

#ifdef HAVE_TREE_H
#include <sys/tree.h>
#else
#include "compat/tree.h"
#endif

#ifdef HAVE_BITSTRING_H
#include <bitstring.h>
#else
#include "compat/bitstring.h"
#endif

#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#ifdef HAVE_PTY_H
#include <pty.h>
#endif

#ifdef HAVE_UTIL_H
#include <util.h>
#endif

#ifdef HAVE_VIS
#include <vis.h>
#else
#include "compat/vis.h"
#endif

#ifdef HAVE_IMSG
#include <imsg.h>
#else
#include "compat/imsg.h"
#endif

#ifdef BROKEN_CMSG_FIRSTHDR
#undef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR(mhdr) \
	((mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? \
	    (struct cmsghdr *)(mhdr)->msg_control :	    \
	    (struct cmsghdr *)NULL)
#endif

#ifndef CMSG_ALIGN
#ifdef _CMSG_DATA_ALIGN
#define CMSG_ALIGN _CMSG_DATA_ALIGN
#else
#define CMSG_ALIGN(len) (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#endif
#endif

#ifndef CMSG_SPACE
#define CMSG_SPACE(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#endif

#ifndef CMSG_LEN
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#endif

#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif

#ifndef FNM_CASEFOLD
#ifdef FNM_IGNORECASE
#define FNM_CASEFOLD FNM_IGNORECASE
#else
#define FNM_CASEFOLD 0
#endif
#endif

#ifndef INFTIM
#define INFTIM -1
#endif

#ifndef WAIT_ANY
#define WAIT_ANY -1
#endif

#ifndef SUN_LEN
#define SUN_LEN(sun) (sizeof (sun)->sun_path)
#endif

#ifndef timercmp
#define	timercmp(tvp, uvp, cmp)						\
	(((tvp)->tv_sec == (uvp)->tv_sec) ?				\
	    ((tvp)->tv_usec cmp (uvp)->tv_usec) :			\
	    ((tvp)->tv_sec cmp (uvp)->tv_sec))
#endif

#ifndef timeradd
#define	timeradd(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;	\
		if ((vvp)->tv_usec >= 1000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_usec -= 1000000;			\
		}							\
	} while (0)
#endif

#ifndef timersub
#define timersub(tvp, uvp, vvp)                                         \
	do {                                                            \
		(vvp)->tv_sec = (tvp)->tv_sec - (uvp)->tv_sec;          \
		(vvp)->tv_usec = (tvp)->tv_usec - (uvp)->tv_usec;       \
		if ((vvp)->tv_usec < 0) {                               \
			(vvp)->tv_sec--;                                \
			(vvp)->tv_usec += 1000000;                      \
		}                                                       \
	} while (0)
#endif

#ifndef TTY_NAME_MAX
#define TTY_NAME_MAX 32
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC CLOCK_REALTIME
#endif

#ifndef HAVE_FLOCK
#define LOCK_SH 0
#define LOCK_EX 0
#define LOCK_NB 0
#define flock(fd, op) (0)
#endif

#ifndef HAVE_EXPLICIT_BZERO
/* explicit_bzero.c */
void		 explicit_bzero(void *, size_t);
#endif

#ifndef HAVE_GETDTABLECOUNT
/* getdtablecount.c */
int		 getdtablecount(void);
#endif

#ifndef HAVE_GETDTABLESIZE
/* getdtablesize.c */
int		 getdtablesize(void);
#endif

#ifndef HAVE_CLOSEFROM
/* closefrom.c */
void		 closefrom(int);
#endif

#ifndef HAVE_STRCASESTR
/* strcasestr.c */
char		*strcasestr(const char *, const char *);
#endif

#ifndef HAVE_STRSEP
/* strsep.c */
char		*strsep(char **, const char *);
#endif

#ifndef HAVE_STRTONUM
/* strtonum.c */
long long	 strtonum(const char *, long long, long long, const char **);
#endif

#ifndef HAVE_STRLCPY
/* strlcpy.c */
size_t	 	 strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_STRLCAT
/* strlcat.c */
size_t	 	 strlcat(char *, const char *, size_t);
#endif

#ifndef HAVE_STRNLEN
/* strnlen.c */
size_t		 strnlen(const char *, size_t);
#endif

#ifndef HAVE_STRNDUP
/* strndup.c */
char		*strndup(const char *, size_t);
#endif

#ifndef HAVE_MEMMEM
/* memmem.c */
void		*memmem(const void *, size_t, const void *, size_t);
#endif

#ifndef HAVE_HTONLL
/* htonll.c */
#undef htonll
uint64_t	 htonll(uint64_t);
#endif

#ifndef HAVE_NTOHLL
/* ntohll.c */
#undef ntohll
uint64_t	 ntohll(uint64_t);
#endif

#ifndef HAVE_GETPEEREID
/* getpeereid.c */
int		getpeereid(int, uid_t *, gid_t *);
#endif

#ifndef HAVE_DAEMON
/* daemon.c */
int	 	 daemon(int, int);
#endif

#ifndef HAVE_GETPROGNAME
/* getprogname.c */
const char	*getprogname(void);
#endif

#ifndef HAVE_SETPROCTITLE
/* setproctitle.c */
void		 setproctitle(const char *, ...);
#endif

#ifndef HAVE_CLOCK_GETTIME
/* clock_gettime.c */
int		 clock_gettime(int, struct timespec *);
#endif

#ifdef _WIN32
/* win32-time.c */
struct tm	*localtime_r(const time_t *, struct tm *);
struct tm	*gmtime_r(const time_t *, struct tm *);
char		*ctime_r(const time_t *, char *);
#endif

#ifndef HAVE_B64_NTOP
/* base64.c */
#undef b64_ntop
#undef b64_pton
int		 b64_ntop(const u_char *, size_t, char *, size_t);
int		 b64_pton(const char *, u_char *, size_t);
#endif

#ifndef HAVE_FDFORKPTY
/* fdforkpty.c */
int		 getptmfd(void);
pid_t		 fdforkpty(int, int *, char *, struct termios *,
		     struct winsize *);
#endif

#ifndef HAVE_FORKPTY
/* forkpty.c */
pid_t		 forkpty(int *, char *, struct termios *, struct winsize *);
#endif

#ifndef HAVE_ASPRINTF
/* asprintf.c */
int		 asprintf(char **, const char *, ...);
int		 vasprintf(char **, const char *, va_list);
#endif

#ifndef HAVE_FGETLN
/* fgetln.c */
char		*fgetln(FILE *, size_t *);
#endif

#ifndef HAVE_GETLINE
/* getline.c */
ssize_t		 getline(char **, size_t *, FILE *);
#endif

#ifndef HAVE_SETENV
/* setenv.c */
int		 setenv(const char *, const char *, int);
int		 unsetenv(const char *);
#endif

#ifndef HAVE_CFMAKERAW
/* cfmakeraw.c */
void		 cfmakeraw(struct termios *);
#endif

#ifndef HAVE_FREEZERO
/* freezero.c */
void		 freezero(void *, size_t);
#endif

#ifndef HAVE_REALLOCARRAY
/* reallocarray.c */
void		*reallocarray(void *, size_t, size_t);
#endif

#ifndef HAVE_RECALLOCARRAY
/* recallocarray.c */
void		*recallocarray(void *, size_t, size_t, size_t);
#endif

#ifdef HAVE_SYSTEMD
/* systemd.c */
int		 systemd_activated(void);
int		 systemd_create_socket(int, char **);
int		 systemd_move_to_new_cgroup(char **);
#endif

#ifdef HAVE_UTF8PROC
/* utf8proc.c */
int		 utf8proc_wcwidth(utf8_wchar);
int		 utf8proc_mbtowc(utf8_wchar *, const char *, size_t);
int		 utf8proc_wctomb(char *, utf8_wchar);
#endif

#ifdef NEED_FUZZING
/* tmux.c */
#define main __weak main
#define regcomp(preg, pattern, cflags) (0)
#define regexec(preg, string, nmatch, pmatch, eflags) (REG_NOMATCH)
#define regfree(preg) ((void)0)
#endif

/* getopt.c */
extern int	 BSDopterr;
extern int	 BSDoptind;
extern int	 BSDoptopt;
extern int	 BSDoptreset;
extern char	*BSDoptarg;
int	BSDgetopt(int, char *const *, const char *);
#define getopt(ac, av, o)  BSDgetopt(ac, av, o)
#define opterr             BSDopterr
#define optind             BSDoptind
#define optopt             BSDoptopt
#define optreset           BSDoptreset
#define optarg             BSDoptarg

#endif /* COMPAT_H */
