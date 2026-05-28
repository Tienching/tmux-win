/* $OpenBSD$ */
/*
 * Copyright (c) 2026 tmux Windows Port Contributors
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

#include "win32-acl.h"

#include <sddl.h>
#include <string.h>

/*
 * SDDL: D = DACL header
 *       (A;;GA;;;OW) = ACE: Allow, no flags, GenericAll, Owner Rights
 *
 * "OW" resolves to the current process owner SID at descriptor build
 * time, so the resulting DACL grants only the calling user. This matches
 * NFR-6 ("endpoint dir ACL = current user SID exclusive GA, semantically
 * equivalent to /tmp/tmux-<UID>/ 0700").
 */
#define WIN32_ACL_OWNER_ONLY_SDDL L"D:(A;;GA;;;OW)"

int
win32_acl_owner_only(SECURITY_ATTRIBUTES *attrs)
{
	PSECURITY_DESCRIPTOR	descriptor = NULL;

	if (attrs == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(attrs, 0, sizeof *attrs);
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
	    WIN32_ACL_OWNER_ONLY_SDDL, SDDL_REVISION_1, &descriptor, NULL))
		return (-1);

	attrs->nLength = sizeof *attrs;
	attrs->lpSecurityDescriptor = descriptor;
	attrs->bInheritHandle = FALSE;
	return (0);
}

void
win32_acl_owner_only_free(SECURITY_ATTRIBUTES *attrs)
{
	if (attrs == NULL)
		return;
	if (attrs->lpSecurityDescriptor != NULL)
		LocalFree(attrs->lpSecurityDescriptor);
	memset(attrs, 0, sizeof *attrs);
}

#endif /* _WIN32 */
