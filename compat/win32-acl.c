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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Internal allocation that groups the security descriptor with the
 * ACL and TOKEN_USER it references, so win32_acl_owner_only_free()
 * can release everything from the lpSecurityDescriptor pointer.
 */
struct win32_acl_owner_only_ctx {
	SECURITY_DESCRIPTOR	descriptor;
	TOKEN_USER		*user;
	ACL			*acl;
};

int
win32_acl_owner_only(SECURITY_ATTRIBUTES *attrs)
{
	struct win32_acl_owner_only_ctx	*ctx = NULL;
	HANDLE				 token = NULL;
	DWORD				 needed = 0, acl_size;
	PSID				 sid;
	int				 ret = -1;

	if (attrs == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(attrs, 0, sizeof *attrs);

	/* Open the current process token and extract the user SID. */
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		goto out;
	GetTokenInformation(token, TokenUser, NULL, 0, &needed);
	if (needed == 0)
		goto out;
	ctx = calloc(1, sizeof *ctx);
	if (ctx == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	ctx->user = malloc(needed);
	if (ctx->user == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (!GetTokenInformation(token, TokenUser, ctx->user, needed,
	    &needed))
		goto out;

	/* Build an ACL with a single ACE granting GENERIC_ALL to the SID. */
	sid = ctx->user->User.Sid;
	acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) -
	    sizeof(DWORD) + GetLengthSid(sid);
	ctx->acl = malloc(acl_size);
	if (ctx->acl == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto out;
	}
	if (!InitializeAcl(ctx->acl, acl_size, ACL_REVISION))
		goto out;
	if (!AddAccessAllowedAce(ctx->acl, ACL_REVISION, GENERIC_ALL, sid))
		goto out;

	/*
	 * Initialise a self-relative security descriptor with the DACL.
	 * The DACL contains the current token user's SID explicitly,
	 * matching the intended per-user tmux endpoint boundary.
	 */
	if (!InitializeSecurityDescriptor(&ctx->descriptor,
	    SECURITY_DESCRIPTOR_REVISION))
		goto out;
	if (!SetSecurityDescriptorDacl(&ctx->descriptor, TRUE, ctx->acl,
	    FALSE))
		goto out;

	attrs->nLength = sizeof *attrs;
	attrs->lpSecurityDescriptor = &ctx->descriptor;
	attrs->bInheritHandle = FALSE;
	ret = 0;

out:
	if (token != NULL)
		CloseHandle(token);
	if (ret != 0) {
		if (ctx != NULL) {
			free(ctx->acl);
			free(ctx->user);
			free(ctx);
		}
	}
	return (ret);
}

void
win32_acl_owner_only_free(SECURITY_ATTRIBUTES *attrs)
{
	struct win32_acl_owner_only_ctx	*ctx;

	if (attrs == NULL)
		return;
	if (attrs->lpSecurityDescriptor == NULL) {
		memset(attrs, 0, sizeof *attrs);
		return;
	}
	ctx = (struct win32_acl_owner_only_ctx *)attrs->lpSecurityDescriptor;
	free(ctx->acl);
	free(ctx->user);
	free(ctx);
	memset(attrs, 0, sizeof *attrs);
}

#endif /* _WIN32 */
