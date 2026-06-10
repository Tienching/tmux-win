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

/*
 * Regex parity probe — tests POSIX regex behaviour so Windows (win32-regex
 * shim) and Linux (glibc regex) results can be compared.
 *
 * Each test prints:   pat | string | rc | so | eo
 * Returns non-zero on the first mismatch with the expected result.
 */

#ifdef _WIN32
#include "win32-regex.h"
#else
#include <regex.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int	nfail;

static int
check(const char *label, const char *pat, const char *s, int cflags,
    int eflags, int expect_rc, int expect_so, int expect_eo)
{
	regex_t		re;
	regmatch_t	pm[1];
	int		rc;
	int		so, eo;

	rc = regcomp(&re, pat, cflags);
	if (rc != 0) {
		char	errbuf[128];
		regerror(rc, &re, errbuf, sizeof(errbuf));
		printf("FAIL %s: pat=%s s=%s regcomp=%d (%s)\n",
		    label, pat, s, rc, errbuf);
		regfree(&re);
		nfail++;
		return (1);
	}

	rc = regexec(&re, s, 1, pm, eflags);
	if (rc == 0) {
		so = (int)pm[0].rm_so;
		eo = (int)pm[0].rm_eo;
	} else {
		so = -1;
		eo = -1;
	}

	printf("%s: pat=%s s=%s rc=%d so=%d eo=%d", label, pat, s, rc, so, eo);

	if (rc != expect_rc || so != expect_so || eo != expect_eo) {
		printf(" FAIL (expected rc=%d so=%d eo=%d)\n",
		    expect_rc, expect_so, expect_eo);
		regfree(&re);
		nfail++;
		return (1);
	}

	printf(" OK\n");
	regfree(&re);
	return (0);
}

int
main(void)
{
	int	ret = 0;

	/* 1. REG_EXTENDED basic match */
	ret |= check("extended-basic", "hello", "say hello world",
	    REG_EXTENDED, 0, 0, 4, 9);

	/* 2. Anchor ^ — must match at start */
	ret |= check("anchor-caret", "^hello", "hello world",
	    REG_EXTENDED, 0, 0, 0, 5);
	ret |= check("anchor-caret-nomatch", "^hello", "say hello",
	    REG_EXTENDED, 0, REG_NOMATCH, -1, -1);

	/* 3. Anchor $ — must match at end */
	ret |= check("anchor-dollar", "world$", "hello world",
	    REG_EXTENDED, 0, 0, 6, 11);
	ret |= check("anchor-dollar-nomatch", "world$", "world hello",
	    REG_EXTENDED, 0, REG_NOMATCH, -1, -1);

	/* 4. REG_NEWLINE with dot — dot should NOT match newline */
	ret |= check("newline-dot", "a.b", "a\nb",
	    REG_EXTENDED | REG_NEWLINE, 0, REG_NOMATCH, -1, -1);
	ret |= check("newline-dot-noflag", "a.b", "a\nb",
	    REG_EXTENDED, 0, 0, 0, 3);

	/* 5. REG_ICASE — case-insensitive match */
	ret |= check("icase", "hello", "HELLO",
	    REG_EXTENDED | REG_ICASE, 0, 0, 0, 5);
	ret |= check("icase-nomatch", "hello", "HELLO",
	    REG_EXTENDED, 0, REG_NOMATCH, -1, -1);

	/* 6. REG_NOTBOL — ^ should not match at start */
	ret |= check("notbol", "^hello", "hello world",
	    REG_EXTENDED, REG_NOTBOL, REG_NOMATCH, -1, -1);

	if (nfail)
		printf("\n%d test(s) FAILED\n", nfail);
	else
		printf("\nAll tests passed\n");

	return (nfail ? 1 : 0);
}
