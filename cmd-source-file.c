/* $OpenBSD$ */

/*
 * Copyright (c) 2008 Tiago Cunha <me@tiagocunha.org>
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
#ifdef _WIN32
#include <sys/stat.h>
#endif

#include <ctype.h>
#include <errno.h>
#ifndef _WIN32
#include <glob.h>
#else
#include <io.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Sources a configuration file.
 */

#define CMD_SOURCE_FILE_DEPTH_LIMIT 50
static u_int cmd_source_file_depth;

static enum cmd_retval	cmd_source_file_exec(struct cmd *, struct cmdq_item *);

#ifdef _WIN32
#define GLOB_NOSPACE 1
#define GLOB_NOMATCH 2
typedef struct {
	size_t	 gl_pathc;
	char	**gl_pathv;
} glob_t;

static int	cmd_source_file_glob(const char *, int, void *, glob_t *);
static int	cmd_source_file_glob_cmp(const void *, const void *);
static void	cmd_source_file_globfree(glob_t *);
static char    *cmd_source_file_expand_path(const char *);
#define glob(pattern, flags, errfunc, pglob) \
	cmd_source_file_glob((pattern), (flags), (errfunc), (pglob))
#define globfree(pglob) cmd_source_file_globfree((pglob))
#endif

const struct cmd_entry cmd_source_file_entry = {
	.name = "source-file",
	.alias = "source",

	.args = { "t:Fnqv", 1, -1, NULL },
	.usage = "[-Fnqv] " CMD_TARGET_PANE_USAGE " path ...",

	.target = { 't', CMD_FIND_PANE, CMD_FIND_CANFAIL },

	.flags = 0,
	.exec = cmd_source_file_exec
};

struct cmd_source_file_data {
	struct cmdq_item	 *item;
	int			  flags;

	struct cmdq_item	 *after;
	enum cmd_retval		  retval;

	u_int			  current;
	char			**files;
	u_int			  nfiles;
};

static enum cmd_retval
cmd_source_file_complete_cb(struct cmdq_item *item, __unused void *data)
{
	struct client	*c = cmdq_get_client(item);

	if (c == NULL) {
		cmd_source_file_depth--;
		log_debug("%s: depth now %u", __func__, cmd_source_file_depth);
	} else {
		c->source_file_depth--;
		log_debug("%s: depth now %u", __func__, c->source_file_depth);
	}

	cfg_print_causes(item);
	return (CMD_RETURN_NORMAL);
}

static void
cmd_source_file_complete(struct client *c, struct cmd_source_file_data *cdata)
{
	struct cmdq_item	*new_item;
	u_int			 i;

	if (cfg_finished) {
		if (cdata->retval == CMD_RETURN_ERROR &&
		    c != NULL &&
		    c->session == NULL)
			c->retval = 1;
		new_item = cmdq_get_callback(cmd_source_file_complete_cb, NULL);
		cmdq_insert_after(cdata->after, new_item);
	}

	for (i = 0; i < cdata->nfiles; i++)
		free(cdata->files[i]);
	free(cdata->files);
	free(cdata);
}

static void
cmd_source_file_done(struct client *c, const char *path, int error,
    int closed, struct evbuffer *buffer, void *data)
{
	struct cmd_source_file_data	*cdata = data;
	struct cmdq_item		*item = cdata->item;
	void				*bdata = EVBUFFER_DATA(buffer);
	size_t				 bsize = EVBUFFER_LENGTH(buffer);
	u_int				 n;
	struct cmdq_item		*new_item;
	struct cmd_find_state		*target = cmdq_get_target(item);

	if (!closed)
		return;

	if (error != 0)
		cmdq_error(item, "%s: %s", strerror(error), path);
	else if (bsize != 0) {
		if (load_cfg_from_buffer(bdata, bsize, path, c, cdata->after,
		    target, cdata->flags, &new_item) < 0)
			cdata->retval = CMD_RETURN_ERROR;
		else if (new_item != NULL)
			cdata->after = new_item;
	}

	n = ++cdata->current;
	if (n < cdata->nfiles)
		file_read(c, cdata->files[n], cmd_source_file_done, cdata);
	else {
		cmd_source_file_complete(c, cdata);
		cmdq_continue(item);
	}
}

static void
cmd_source_file_add(struct cmd_source_file_data *cdata, const char *path)
{
	log_debug("%s: %s", __func__, path);
	cdata->files = xreallocarray(cdata->files, cdata->nfiles + 1,
	    sizeof *cdata->files);
	cdata->files[cdata->nfiles++] = xstrdup(path);
}

static char *
cmd_source_file_quote_for_glob(const char *path)
{
#ifdef _WIN32
	return (xstrdup(path));
#else
	char		*quoted = xmalloc(2 * strlen(path) + 1), *q = quoted;
	const char	*p = path;

	while (*p != '\0') {
		if ((u_char)*p < 128 && !isalnum((u_char)*p) && *p != '/')
			*q++ = '\\';
		*q++ = *p++;
	}
	*q = '\0';
	return (quoted);
#endif
}

#ifdef _WIN32
static char *
cmd_source_file_expand_path(const char *path)
{
	struct environ_entry	*value;
	const char		*end;
	char			*name, *expanded;

	if (*path != '%')
		return (xstrdup(path));

	end = strchr(path + 1, '%');
	if (end == NULL || end == path + 1)
		return (xstrdup(path));

	name = xstrndup(path + 1, end - path - 1);
	value = environ_find(global_environ, name);
	free(name);
	if (value == NULL || value->value == NULL)
		return (xstrdup(path));

	xasprintf(&expanded, "%s%s", value->value, end + 1);
	return (expanded);
}

static int
cmd_source_file_path_separator(int ch)
{
	return (ch == '/' || ch == '\\');
}

static int
cmd_source_file_glob_add(glob_t *g, const char *path)
{
	g->gl_pathv = xreallocarray(g->gl_pathv, g->gl_pathc + 1,
	    sizeof *g->gl_pathv);
	g->gl_pathv[g->gl_pathc++] = xstrdup(path);
	return (0);
}

static int
cmd_source_file_glob_magic(const char *pattern)
{
	return (strpbrk(pattern, "*?[") != NULL);
}

static int
cmd_source_file_glob_segment_magic(const char *segment)
{
	return (strpbrk(segment, "*?[") != NULL);
}

static int
cmd_source_file_exists(const char *path)
{
	struct stat	sb;

	return (stat(path, &sb) == 0);
}

static int
cmd_source_file_is_directory(const char *path)
{
	struct stat	sb;

	if (stat(path, &sb) != 0)
		return (0);
	return (S_ISDIR(sb.st_mode));
}

static char *
cmd_source_file_path_join(const char *dir, const char *name)
{
	char	*path;
	size_t	len;

	if (*dir == '\0' || strcmp(dir, ".") == 0)
		return (xstrdup(name));

	len = strlen(dir);
	if (cmd_source_file_path_separator((u_char)dir[len - 1]))
		xasprintf(&path, "%s%s", dir, name);
	else
		xasprintf(&path, "%s\\%s", dir, name);
	return (path);
}

static char *
cmd_source_file_search_pattern(const char *dir)
{
	char	*path;
	size_t	len;

	if (*dir == '\0' || strcmp(dir, ".") == 0)
		return (xstrdup("*"));

	len = strlen(dir);
	if (cmd_source_file_path_separator((u_char)dir[len - 1]))
		xasprintf(&path, "%s*", dir);
	else
		xasprintf(&path, "%s\\*", dir);
	return (path);
}

static char *
cmd_source_file_glob_root(const char *pattern, const char **rest)
{
	const char	*cp;

	if (isalpha((u_char)pattern[0]) && pattern[1] == ':') {
		if (cmd_source_file_path_separator((u_char)pattern[2])) {
			*rest = pattern + 3;
			return (xstrndup(pattern, 3));
		}
		*rest = pattern + 2;
		return (xstrndup(pattern, 2));
	}

	if (cmd_source_file_path_separator((u_char)pattern[0]) &&
	    cmd_source_file_path_separator((u_char)pattern[1])) {
		cp = pattern + 2;
		while (*cp != '\0' &&
		    !cmd_source_file_path_separator((u_char)*cp))
			cp++;
		if (*cp != '\0')
			cp++;
		while (*cp != '\0' &&
		    !cmd_source_file_path_separator((u_char)*cp))
			cp++;
		if (*cp != '\0')
			cp++;
		*rest = cp;
		return (xstrndup(pattern, cp - pattern));
	}

	if (cmd_source_file_path_separator((u_char)pattern[0])) {
		*rest = pattern + 1;
		return (xstrndup(pattern, 1));
	}

	*rest = pattern;
	return (xstrdup(""));
}

static int
cmd_source_file_glob_walk(glob_t *g, const char *dir, const char *pattern)
{
	struct _finddata_t	 data;
	intptr_t		 handle;
	const char		*next;
	char			*segment, *path, *search;
	int			 matched = 0, last, fnm_flags = FNM_CASEFOLD;

	while (cmd_source_file_path_separator((u_char)*pattern))
		pattern++;
	if (*pattern == '\0') {
		if (*dir != '\0' && cmd_source_file_exists(dir)) {
			cmd_source_file_glob_add(g, dir);
			return (1);
		}
		return (0);
	}

	next = pattern;
	while (*next != '\0' &&
	    !cmd_source_file_path_separator((u_char)*next))
		next++;
	segment = xstrndup(pattern, next - pattern);
	while (cmd_source_file_path_separator((u_char)*next))
		next++;
	last = (*next == '\0');

	if (!cmd_source_file_glob_segment_magic(segment)) {
		path = cmd_source_file_path_join(dir, segment);
		if (last) {
			if (cmd_source_file_exists(path)) {
				cmd_source_file_glob_add(g, path);
				matched = 1;
			}
		} else if (cmd_source_file_is_directory(path))
			matched = cmd_source_file_glob_walk(g, path, next);
		free(path);
		free(segment);
		return (matched);
	}

	search = cmd_source_file_search_pattern(dir);
	handle = _findfirst(search, &data);
	free(search);
	if (handle == -1) {
		free(segment);
		return (0);
	}
	do {
		if (strcmp(data.name, ".") == 0 || strcmp(data.name, "..") == 0)
			continue;
		if (fnmatch(segment, data.name, fnm_flags) != 0)
			continue;
		path = cmd_source_file_path_join(dir, data.name);
		if (last) {
			cmd_source_file_glob_add(g, path);
			matched = 1;
		} else if (data.attrib & _A_SUBDIR)
			matched += cmd_source_file_glob_walk(g, path, next);
		free(path);
	} while (_findnext(handle, &data) == 0);
	_findclose(handle);

	free(segment);
	return (matched);
}

static int
cmd_source_file_glob(const char *pattern, __unused int flags,
    __unused void *errfunc, glob_t *g)
{
	struct stat		 sb;
	const char		*rest;
	char			*root;
	int			 matched;

	memset(g, 0, sizeof *g);
	if (!cmd_source_file_glob_magic(pattern)) {
		if (stat(pattern, &sb) != 0)
			return (GLOB_NOMATCH);
		return (cmd_source_file_glob_add(g, pattern));
	}

	root = cmd_source_file_glob_root(pattern, &rest);
	matched = cmd_source_file_glob_walk(g, root, rest);
	free(root);
	if (matched == 0)
		return (GLOB_NOMATCH);
	qsort(g->gl_pathv, g->gl_pathc, sizeof *g->gl_pathv,
	    cmd_source_file_glob_cmp);
	return (0);
}

static int
cmd_source_file_glob_cmp(const void *a0, const void *b0)
{
	const char *const	*a = a0, *const *b = b0;

	return (strcmp(*a, *b));
}

static void
cmd_source_file_globfree(glob_t *g)
{
	size_t	i;

	for (i = 0; i < g->gl_pathc; i++)
		free(g->gl_pathv[i]);
	free(g->gl_pathv);
	memset(g, 0, sizeof *g);
}
#endif

static enum cmd_retval
cmd_source_file_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args			*args = cmd_get_args(self);
	struct cmd_source_file_data	*cdata;
	struct client			*c = cmdq_get_client(item);
	enum cmd_retval			 retval = CMD_RETURN_NORMAL;
	char				*pattern, *cwd, *expanded = NULL;
#ifdef _WIN32
	char				*path_expanded = NULL;
#endif
	const char			*path, *error;
	glob_t				 g;
	int				 result, parse_flags;
	u_int				 i, j;

	if (c == NULL) {
		if (cmd_source_file_depth >= CMD_SOURCE_FILE_DEPTH_LIMIT) {
			cmdq_error(item, "too many nested files");
			return (CMD_RETURN_ERROR);
		}
		cmd_source_file_depth++;
		log_debug("%s: depth now %u", __func__, cmd_source_file_depth);
	} else {
		if (c->source_file_depth >= CMD_SOURCE_FILE_DEPTH_LIMIT) {
			cmdq_error(item, "too many nested files");
			return (CMD_RETURN_ERROR);
		}
		c->source_file_depth++;
		log_debug("%s: depth now %u", __func__, c->source_file_depth);
	}

	cdata = xcalloc(1, sizeof *cdata);
	cdata->item = item;

	if (args_has(args, 'q'))
		cdata->flags |= CMD_PARSE_QUIET;
	if (args_has(args, 'n'))
		cdata->flags |= CMD_PARSE_PARSEONLY;
	if (c == NULL || ~c->flags & CLIENT_CONTROL) {
		parse_flags = cmd_get_parse_flags(self);
		if (args_has(args, 'v') || (parse_flags & CMD_PARSE_VERBOSE))
			cdata->flags |= CMD_PARSE_VERBOSE;
	}

	cwd = cmd_source_file_quote_for_glob(server_client_get_cwd(c, NULL));

	for (i = 0; i < args_count(args); i++) {
		path = args_string(args, i);
		if (args_has(args, 'F')) {
			free(expanded);
			expanded = format_single_from_target(item, path);
			path = expanded;
		}
#ifdef _WIN32
		free(path_expanded);
		path_expanded = cmd_source_file_expand_path(path);
		path = path_expanded;
#endif
		if (strcmp(path, "-") == 0) {
			cmd_source_file_add(cdata, "-");
			continue;
		}

		if (
#ifdef _WIN32
		    path_is_absolute(path)
#else
		    *path == '/'
#endif
		    )
			pattern = xstrdup(path);
		else
#ifdef _WIN32
			/*
			 * Use the platform-native separator on Windows so the
			 * resulting pattern is consistent with path_is_absolute
			 * checks elsewhere and survives round-trips through
			 * APIs that tokenise on '\\'.
			 */
			xasprintf(&pattern, "%s\\%s", cwd, path);
#else
			xasprintf(&pattern, "%s/%s", cwd, path);
#endif
		log_debug("%s: %s", __func__, pattern);

		if ((result = glob(pattern, 0, NULL, &g)) != 0) {
			if (result != GLOB_NOMATCH ||
			    (~cdata->flags & CMD_PARSE_QUIET)) {
				if (result == GLOB_NOMATCH)
					error = strerror(ENOENT);
				else if (result == GLOB_NOSPACE)
					error = strerror(ENOMEM);
				else
					error = strerror(EINVAL);
				cmdq_error(item, "%s: %s", error, path);
				retval = CMD_RETURN_ERROR;
			}
			globfree(&g);
			free(pattern);
			continue;
		}
		free(pattern);

		for (j = 0; j < g.gl_pathc; j++)
			cmd_source_file_add(cdata, g.gl_pathv[j]);
		globfree(&g);
	}
	free(expanded);
#ifdef _WIN32
	free(path_expanded);
#endif

	cdata->after = item;
	cdata->retval = retval;

	if (cdata->nfiles != 0) {
		file_read(c, cdata->files[0], cmd_source_file_done, cdata);
		retval = CMD_RETURN_WAIT;
	} else
		cmd_source_file_complete(c, cdata);

	free(cwd);
	return (retval);
}
