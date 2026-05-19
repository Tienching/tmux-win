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

#ifndef TMUX_WIN32_REGEX_H
#define TMUX_WIN32_REGEX_H

#ifdef _WIN32

#include <stddef.h>

typedef long regoff_t;

typedef struct {
	void	*opaque;
	int	 cflags;
} regex_t;

typedef struct {
	regoff_t rm_so;
	regoff_t rm_eo;
} regmatch_t;

#ifndef REG_EXTENDED
#define REG_EXTENDED 0x01
#endif
#ifndef REG_ICASE
#define REG_ICASE 0x02
#endif
#ifndef REG_NOSUB
#define REG_NOSUB 0x04
#endif
#ifndef REG_NEWLINE
#define REG_NEWLINE 0x08
#endif
#ifndef REG_NOTBOL
#define REG_NOTBOL 0x01
#endif
#ifndef REG_NOMATCH
#define REG_NOMATCH 1
#endif
#ifndef REG_BADPAT
#define REG_BADPAT 2
#endif
#ifndef REG_ESPACE
#define REG_ESPACE 3
#endif

#ifdef __cplusplus
extern "C" {
#endif
int	regcomp(regex_t *, const char *, int);
int	regexec(const regex_t *, const char *, size_t, regmatch_t [], int);
size_t	regerror(int, const regex_t *, char *, size_t);
void	regfree(regex_t *);
#ifdef __cplusplus
}
#endif

#endif

#endif
