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

#ifndef TMUX_WIN32_ACL_H
#define TMUX_WIN32_ACL_H

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

/*
 * Build a SECURITY_ATTRIBUTES that grants GENERIC_ALL only to the current
 * owner (NFR-6, design sec 5). The descriptor is allocated by Windows;
 * caller releases it via win32_acl_owner_only_free().
 *
 * Internally uses ConvertStringSecurityDescriptorToSecurityDescriptorW
 * with the SDDL string "D:(A;;GA;;;OW)" (DACL = allow GENERIC_ALL to
 * Owner). Compared to building an explicit ACE with GetTokenInformation
 * (compat/win32-ipc.c::win32_ipc_security_init), the SDDL approach is
 * three orders of magnitude cheaper to set up and avoids a double malloc.
 */
int	win32_acl_owner_only(SECURITY_ATTRIBUTES *attrs);
void	win32_acl_owner_only_free(SECURITY_ATTRIBUTES *attrs);

#endif /* _WIN32 */

#endif /* TMUX_WIN32_ACL_H */
