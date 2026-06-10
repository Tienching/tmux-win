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

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sys/types.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#endif

#include <ctype.h>
#include <errno.h>
#ifndef _WIN32
#include <fcntl.h>
#include <langinfo.h>
#endif
#include <locale.h>
#ifndef _WIN32
#include <pwd.h>
#endif
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "tmux.h"

#ifdef _WIN32
#include "compat/win32-command.h"
#include "compat/win32-endpoint.h"
#include "compat/win32-socketpair.h"
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#endif

struct options	*global_options;	/* server options */
struct options	*global_s_options;	/* session options */
struct options	*global_w_options;	/* window options */
struct environ	*global_environ;

struct timeval	 start_time;
const char	*socket_path;
int		 ptm_fd = -1;
const char	*shell_command;

static __dead void	 usage(int);
static char		*make_label(const char *, char **);

static int		 areshell(const char *);
static const char	*getshell(void);

static __dead void
usage(int status)
{
	fprintf(status ? stderr : stdout,
	    "usage: %s [-2CDhlNuVv] [-c shell-command] [-f file] [-L socket-name]\n"
	    "            [-S socket-path] [-T features] [command [flags]]\n",
	    getprogname());
	exit(status);
}

static const char *
getshell(void)
{
#ifdef _WIN32
	const char	*shell;

	if ((shell = getenv("ComSpec")) != NULL && checkshell(shell))
		return (shell);
	if ((shell = getenv("SHELL")) != NULL && checkshell(shell))
		return (shell);
	return ("C:\\Windows\\System32\\cmd.exe");
#else
	struct passwd	*pw;
	const char	*shell;

	shell = getenv("SHELL");
	if (checkshell(shell))
		return (shell);

	pw = getpwuid(getuid());
	if (pw != NULL && checkshell(pw->pw_shell))
		return (pw->pw_shell);

	return (_PATH_BSHELL);
#endif
}

const char *
get_default_shell(void)
{
	return (getshell());
}

int
checkshell(const char *shell)
{
#ifdef _WIN32
	char	 resolved[MAX_PATH];
	DWORD	 n;

	if (shell == NULL || *shell == '\0')
		return (0);
	if (areshell(shell))
		return (0);
	n = SearchPathA(NULL, shell, ".exe", sizeof resolved, resolved, NULL);
	if (n == 0 || n >= sizeof resolved)
		return (0);
	if (areshell(resolved))
		return (0);
	return (1);
#else
	if (shell == NULL || *shell != '/')
		return (0);
	if (areshell(shell))
		return (0);
	if (access(shell, X_OK) != 0)
		return (0);
	return (1);
#endif
}

int
path_is_absolute(const char *path)
{
#ifdef _WIN32
	if (path == NULL || *path == '\0')
		return (0);
	if (*path == '/' || *path == '\\')
		return (1);
	if (isalpha((u_char)path[0]) && path[1] == ':' &&
	    (path[2] == '/' || path[2] == '\\'))
		return (1);
	return (0);
#else
	return (path != NULL && *path == '/');
#endif
}

int
path_is_directory(const char *path)
{
#ifdef _WIN32
	DWORD	 attrs;
	wchar_t	*wide_path;

	if (path == NULL || *path == '\0')
		return (0);
	wide_path = win32_utf8_to_wide_path(path);
	if (wide_path == NULL)
		return (0);
	attrs = GetFileAttributesW(wide_path);
	free(wide_path);
	if (attrs == INVALID_FILE_ATTRIBUTES)
		return (0);
	return (!!(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
	struct stat	sb;

	if (path == NULL || *path == '\0')
		return (0);
	if (stat(path, &sb) != 0)
		return (0);
	return (S_ISDIR(sb.st_mode));
#endif
}

static int
areshell(const char *shell)
{
	const char	*progname, *ptr;

	if ((ptr = strrchr(shell, '/')) != NULL)
		ptr++;
#ifdef _WIN32
	else if ((ptr = strrchr(shell, '\\')) != NULL)
		ptr++;
#endif
	else
		ptr = shell;
	progname = getprogname();
	if (*progname == '-')
		progname++;
	if (strcmp(ptr, progname) == 0)
		return (1);
	return (0);
}

static char *
expand_path(const char *path, const char *home)
{
	char			*expanded, *name;
	const char		*end;
	struct environ_entry	*value;

	if (strncmp(path, "~/", 2) == 0 ||
	    strncmp(path, "~\\", 2) == 0) {
		if (home == NULL)
			return (NULL);
		xasprintf(&expanded, "%s%s", home, path + 1);
		return (expanded);
	}

	if (*path == '$') {
#ifdef _WIN32
		end = strpbrk(path, "/\\");
#else
		end = strchr(path, '/');
#endif
		if (end == NULL)
			name = xstrdup(path + 1);
		else
			name = xstrndup(path + 1, end - path - 1);
		value = environ_find(global_environ, name);
		free(name);
		if (value == NULL)
			return (NULL);
		if (end == NULL)
			end = "";
		xasprintf(&expanded, "%s%s", value->value, end);
		return (expanded);
	}

#ifdef _WIN32
	if (*path == '%') {
		end = strchr(path + 1, '%');
		if (end == NULL || end == path + 1)
			return (NULL);
		name = xstrndup(path + 1, end - path - 1);
		value = environ_find(global_environ, name);
		free(name);
		if (value == NULL)
			return (NULL);
		xasprintf(&expanded, "%s%s", value->value, end + 1);
		return (expanded);
	}
#endif

	return (xstrdup(path));
}

#ifdef _WIN32
static int
expand_paths_drive_colon(const char *start, const char *cp)
{
	return (cp == start + 1 && isalpha((unsigned char)start[0]));
}

static char *
expand_paths_next(char **tmp)
{
	char	*start = *tmp, *cp;

	if (start == NULL)
		return (NULL);
	for (cp = start; *cp != '\0'; cp++) {
		if (*cp == ';' ||
		    (*cp == ':' && !expand_paths_drive_colon(start, cp))) {
			*cp = '\0';
			*tmp = cp + 1;
			return (start);
		}
	}
	*tmp = NULL;
	return (start);
}
#endif

static void
expand_paths(const char *s, char ***paths, u_int *n, int no_realpath)
{
	const char	*home = find_home();
	char		*copy, *next, *tmp, *expanded;
#ifndef _WIN32
	char		 resolved[PATH_MAX];
#endif
	char		*path;
	u_int		 i;

#ifdef _WIN32
	(void)no_realpath;
#endif
	*paths = NULL;
	*n = 0;

	copy = tmp = xstrdup(s);
#ifdef _WIN32
	while ((next = expand_paths_next(&tmp)) != NULL) {
#else
	while ((next = strsep(&tmp, ":")) != NULL) {
#endif
		expanded = expand_path(next, home);
		if (expanded == NULL) {
			log_debug("%s: invalid path: %s", __func__, next);
			continue;
		}
#ifdef _WIN32
		path = expanded;
#else
		if (no_realpath)
			path = expanded;
		else {
			if (realpath(expanded, resolved) == NULL) {
				log_debug("%s: realpath(\"%s\") failed: %s", __func__,
			  expanded, strerror(errno));
				free(expanded);
				continue;
			}
			path = xstrdup(resolved);
			free(expanded);
		}
#endif
		for (i = 0; i < *n; i++) {
			if (strcmp(path, (*paths)[i]) == 0)
				break;
		}
		if (i != *n) {
			log_debug("%s: duplicate path: %s", __func__, path);
			free(path);
			continue;
		}
		*paths = xreallocarray(*paths, (*n) + 1, sizeof *paths);
		(*paths)[(*n)++] = path;
	}
	free(copy);
}

static char *
make_label(const char *label, char **cause)
{
#ifdef _WIN32
	char	*clean;
	wchar_t	*wpath;
	char	*utf8;

	*cause = NULL;
	if (label == NULL)
		label = "default";
	if ((clean = clean_name(label, "\\/:*?\"<>|")) == NULL) {
		xasprintf(cause, "invalid socket name: %s", label);
		return (NULL);
	}
	if (win32_endpoint_resolve_path(clean, &wpath) != 0) {
		xasprintf(cause, "couldn't create Windows endpoint path");
		free(clean);
		return (NULL);
	}
	free(clean);
	/* Convert wide path back to UTF-8 for internal use. */
	utf8 = win32_wide_to_utf8_path(wpath);
	free(wpath);
	if (utf8 == NULL) {
		xasprintf(cause, "couldn't convert endpoint path to UTF-8");
		return (NULL);
	}
	return (utf8);
#else
	char		**paths, *path, *base;
	u_int		  i, n;
	struct stat	  sb;
	uid_t		  uid;

	*cause = NULL;
	if (label == NULL)
		label = "default";
	uid = getuid();

	expand_paths(TMUX_SOCK, &paths, &n, 0);
	if (n == 0) {
		xasprintf(cause, "no suitable socket path");
		return (NULL);
	}
	path = paths[0]; /* can only have one socket! */
	for (i = 1; i < n; i++)
		free(paths[i]);
	free(paths);

	xasprintf(&base, "%s/tmux-%ld", path, (long)uid);
	free(path);
	if (mkdir(base, S_IRWXU) != 0 && errno != EEXIST) {
		xasprintf(cause, "couldn't create directory %s (%s)", base,
		    strerror(errno));
		goto fail;
	}
	if (lstat(base, &sb) != 0) {
		xasprintf(cause, "couldn't read directory %s (%s)", base,
		    strerror(errno));
		goto fail;
	}
	if (!S_ISDIR(sb.st_mode)) {
		xasprintf(cause, "%s is not a directory", base);
		goto fail;
	}
	if (sb.st_uid != uid || (sb.st_mode & TMUX_SOCK_PERM) != 0) {
		xasprintf(cause, "directory %s has unsafe permissions", base);
		goto fail;
	}
	xasprintf(&path, "%s/%s", base, label);
	free(base);
	return (path);

fail:
	free(base);
	return (NULL);
#endif
}

char *
shell_argv0(const char *shell, int is_login)
{
	const char	*slash, *name;
	char		*argv0;

	slash = strrchr(shell, '/');
#ifdef _WIN32
	if (slash == NULL)
		slash = strrchr(shell, '\\');
#endif
	if (slash != NULL && slash[1] != '\0')
		name = slash + 1;
	else
		name = shell;
	if (is_login)
		xasprintf(&argv0, "-%s", name);
	else
		xasprintf(&argv0, "%s", name);
	return (argv0);
}

void
setblocking(int fd, int state)
{
#ifdef _WIN32
	win32_socket_set_blocking((uintptr_t)fd, state);
#else
	int mode;

	if ((mode = fcntl(fd, F_GETFL)) != -1) {
		if (!state)
			mode |= O_NONBLOCK;
		else
			mode &= ~O_NONBLOCK;
		fcntl(fd, F_SETFL, mode);
	}
#endif
}

uint64_t
get_timer(void)
{
#ifdef _WIN32
	return (GetTickCount64());
#else
	struct timespec	ts;

	/*
	 * We want a timestamp in milliseconds suitable for time measurement,
	 * so prefer the monotonic clock.
	 */
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		clock_gettime(CLOCK_REALTIME, &ts);
	return ((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
#endif
}

char *
clean_name(const char *name, const char* forbid)
{
	char	*copy, *cp, *new_name;

	if (*name == '\0' || !utf8_isvalid(name))
		return (NULL);
	copy = xstrdup(name);
	for (cp = copy; *cp != '\0'; cp++) {
		if (strchr(forbid, *cp) != NULL)
			*cp = '_';
	}
	utf8_stravis(&new_name, copy, VIS_OCTAL|VIS_CSTYLE|VIS_TAB|VIS_NL);
	free(copy);
	return (new_name);
}

const char *
sig2name(int signo)
{
     static char	s[11];

#ifdef HAVE_SYS_SIGNAME
     if (signo > 0 && signo < NSIG)
	     return (sys_signame[signo]);
#endif
     xsnprintf(s, sizeof s, "%d", signo);
     return (s);
}

const char *
find_cwd(void)
{
#ifdef _WIN32
	static char	cwd[MAX_PATH];
	DWORD		n;

	n = GetCurrentDirectoryA(sizeof cwd, cwd);
	if (n == 0 || n >= sizeof cwd)
		return (NULL);
	return (cwd);
#else
	char		 resolved1[PATH_MAX], resolved2[PATH_MAX];
	static char	 cwd[PATH_MAX];
	const char	*pwd;

	if (getcwd(cwd, sizeof cwd) == NULL)
		return (NULL);
	if ((pwd = getenv("PWD")) == NULL || *pwd == '\0')
		return (cwd);

	/*
	 * We want to use PWD so that symbolic links are maintained,
	 * but only if it matches the actual working directory.
	 */
	if (realpath(pwd, resolved1) == NULL)
		return (cwd);
	if (realpath(cwd, resolved2) == NULL)
		return (cwd);
	if (strcmp(resolved1, resolved2) != 0)
		return (cwd);
	return (pwd);
#endif
}

const char *
find_home(void)
{
#ifdef _WIN32
	static char	 home[MAX_PATH];
	const char	*value;
	DWORD		 n;

	if ((value = getenv("USERPROFILE")) != NULL && *value != '\0')
		return (value);
	if (home[0] != '\0')
		return (home);
	if ((value = getenv("HOMEDRIVE")) != NULL && *value != '\0') {
		strlcpy(home, value, sizeof home);
		if ((value = getenv("HOMEPATH")) != NULL)
			strlcat(home, value, sizeof home);
		if (home[0] != '\0')
			return (home);
	}
	if ((value = getenv("TEMP")) != NULL && *value != '\0')
		return (value);
	if ((value = getenv("TMP")) != NULL && *value != '\0')
		return (value);
	n = GetTempPathA(sizeof home, home);
	if (n != 0 && n < sizeof home && home[0] != '\0')
		return (home);
	n = GetWindowsDirectoryA(home, sizeof home);
	if (n != 0 && n < sizeof home && home[0] != '\0')
		return (home);
	return (NULL);
#else
	struct passwd		*pw;
	static const char	*home;

	if (home != NULL)
		return (home);

	home = getenv("HOME");
	if (home == NULL || *home == '\0') {
		pw = getpwuid(getuid());
		if (pw != NULL)
			home = pw->pw_dir;
		else
			home = NULL;
	}

	return (home);
#endif
}

const char *
find_default_cwd(void)
{
	const char	*path;

	if ((path = find_home()) != NULL)
		return (path);
#ifdef _WIN32
	if ((path = find_cwd()) != NULL)
		return (path);
	return ("C:\\");
#else
	return ("/");
#endif
}

const char *
getversion(void)
{
	return (TMUX_VERSION);
}

int
main(int argc, char **argv)
{
	char					*path = NULL, *label = NULL;
	char					*cause, **var;
	const char				*s, *cwd, *slash;
	int					 opt, keys, feat = 0, fflag = 0;
	uint64_t				 flags = 0;
	const struct options_table_entry	*oe;
	u_int					 i;

#ifdef _WIN32
	if (setlocale(LC_CTYPE, "") == NULL)
		errx(1, "invalid LC_ALL, LC_CTYPE or LANG");
#else
	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL &&
	    setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		if (setlocale(LC_CTYPE, "") == NULL)
			errx(1, "invalid LC_ALL, LC_CTYPE or LANG");
		s = nl_langinfo(CODESET);
		if (strcasecmp(s, "UTF-8") != 0 && strcasecmp(s, "UTF8") != 0)
			errx(1, "need UTF-8 locale (LC_CTYPE) but have %s", s);
	}
#endif

	setlocale(LC_TIME, "");
	tzset();

	if (**argv == '-')
		flags = CLIENT_LOGIN;

	global_environ = environ_create();
	for (var = TMUX_ENVIRON; *var != NULL; var++)
		environ_put(global_environ, *var, 0);
	if ((cwd = find_cwd()) != NULL)
		environ_set(global_environ, "PWD", 0, "%s", cwd);
	expand_paths(TMUX_CONF, &cfg_files, &cfg_nfiles, 1);

	while ((opt = getopt(argc, argv, "2c:CDdf:hlL:NqS:T:uUvV")) != -1) {
		switch (opt) {
		case '2':
			tty_add_features(&feat, "256", ":,");
			break;
		case 'c':
			shell_command = optarg;
			break;
		case 'D':
			flags |= CLIENT_NOFORK;
			break;
		case 'C':
			if (flags & CLIENT_CONTROL)
				flags |= CLIENT_CONTROLCONTROL;
			else
				flags |= CLIENT_CONTROL;
			break;
		case 'f':
			if (!fflag) {
				fflag = 1;
				cfg_user_files = 1;
				for (i = 0; i < cfg_nfiles; i++)
					free(cfg_files[i]);
				cfg_nfiles = 0;
			}
			cfg_files = xreallocarray(cfg_files, cfg_nfiles + 1,
			    sizeof *cfg_files);
			cfg_files[cfg_nfiles++] = xstrdup(optarg);
			cfg_quiet = 0;
			break;
		case 'h':
			usage(0);
		case 'V':
			printf("tmux %s\n", getversion());
			exit(0);
		case 'l':
			flags |= CLIENT_LOGIN;
			break;
		case 'L':
			free(label);
			label = xstrdup(optarg);
			break;
		case 'N':
			flags |= CLIENT_NOSTARTSERVER;
			break;
		case 'q':
			break;
		case 'S':
			free(path);
			path = xstrdup(optarg);
			break;
		case 'T':
			tty_add_features(&feat, optarg, ":,");
			break;
		case 'u':
			flags |= CLIENT_UTF8;
			break;
		case 'v':
			log_add_level();
			break;
		default:
			usage(1);
		}
	}
	argc -= optind;
	argv += optind;

	if (shell_command != NULL && argc != 0)
		usage(1);
	if ((flags & CLIENT_NOFORK) && argc != 0)
		usage(1);

#ifdef _WIN32
	ptm_fd = -1;
#else
	if ((ptm_fd = getptmfd()) == -1)
		err(1, "getptmfd");
#endif
	if (pledge("stdio rpath wpath cpath flock fattr unix getpw sendfd "
	    "recvfd proc exec tty ps", NULL) != 0)
		err(1, "pledge");

	/*
	 * tmux is a UTF-8 terminal, so if TMUX is set, assume UTF-8.
	 * Otherwise, if the user has set LC_ALL, LC_CTYPE or LANG to contain
	 * UTF-8, it is a safe assumption that either they are using a UTF-8
	 * terminal, or if not they know that output from UTF-8-capable
	 * programs may be wrong.
	 */
	if (getenv("TMUX") != NULL)
		flags |= CLIENT_UTF8;
	else {
		s = getenv("LC_ALL");
		if (s == NULL || *s == '\0')
			s = getenv("LC_CTYPE");
		if (s == NULL || *s == '\0')
			s = getenv("LANG");
		if (s == NULL || *s == '\0')
			s = "";
		if (strcasestr(s, "UTF-8") != NULL ||
		    strcasestr(s, "UTF8") != NULL)
			flags |= CLIENT_UTF8;
	}
#ifdef _WIN32
	/*
	 * The Windows console host and the ConPTY pane bridge are always driven
	 * in UTF-8 by this port (the stdin/stdout code pages are forced to
	 * CP_UTF8), and Windows does not expose UTF-8 through the LC_ALL,
	 * LC_CTYPE or LANG variables. Without this, non-ASCII cells are
	 * rendered as underscores by tty_check_codeset(), so always treat the
	 * client as UTF-8.
	 */
	flags |= CLIENT_UTF8;
#endif

	global_options = options_create(NULL);
	global_s_options = options_create(NULL);
	global_w_options = options_create(NULL);
	for (oe = options_table; oe->name != NULL; oe++) {
		if (oe->scope & OPTIONS_TABLE_SERVER)
			options_default(global_options, oe);
		if (oe->scope & OPTIONS_TABLE_SESSION)
			options_default(global_s_options, oe);
		if (oe->scope & OPTIONS_TABLE_WINDOW)
			options_default(global_w_options, oe);
	}

	/*
	 * The default shell comes from SHELL or from the user's passwd entry
	 * if available.
	 */
	options_set_string(global_s_options, "default-shell", 0, "%s",
	    getshell());

	/* Override keys to vi if VISUAL or EDITOR are set. */
	if ((s = getenv("VISUAL")) != NULL || (s = getenv("EDITOR")) != NULL) {
		options_set_string(global_options, "editor", 0, "%s", s);
		slash = strrchr(s, '/');
#ifdef _WIN32
		if (strrchr(s, '\\') != NULL &&
		    (slash == NULL || strrchr(s, '\\') > slash))
			slash = strrchr(s, '\\');
#endif
		if (slash != NULL)
			s = slash + 1;
		if (strstr(s, "vi") != NULL)
			keys = MODEKEY_VI;
		else
			keys = MODEKEY_EMACS;
		options_set_number(global_s_options, "status-keys", keys);
		options_set_number(global_w_options, "mode-keys", keys);
	}

	/*
	 * If socket is specified on the command-line with -S or -L, it is
	 * used. Otherwise, $TMUX is checked and if that fails "default" is
	 * used.
	 */
	if (path == NULL && label == NULL) {
		s = getenv("TMUX");
		if (s != NULL && *s != '\0' && *s != ',') {
			path = xstrdup(s);
			path[strcspn(path, ",")] = '\0';
		}
	}
	if (path == NULL) {
		if ((path = make_label(label, &cause)) == NULL) {
			if (cause != NULL) {
				fprintf(stderr, "%s\n", cause);
				free(cause);
			}
			exit(1);
		}
		flags |= CLIENT_DEFAULTSOCKET;
	}
	socket_path = path;
	free(label);

	/* Pass control to the client. */
	exit(client_main(osdep_event_init(), argc, argv, flags, feat));
}
