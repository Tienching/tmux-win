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

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <errno.h>
#include <stddef.h>

#include "win32-errno.h"

/*
 * Hand-rolled translation table. The list deliberately omits ranges that
 * already round-trip through MSVCRT _dosmaperr() because tmux mostly hits
 * the IPC / process surface, where MSVCRT's table is incomplete (e.g.
 * ERROR_PIPE_BUSY mapping is wrong on older CRTs).
 */
struct win32_errno_entry {
	unsigned long	win32_code;
	int		posix_errno;
};

static const struct win32_errno_entry win32_errno_table[] = {
	{ ERROR_SUCCESS,                0          },
	{ ERROR_FILE_NOT_FOUND,         ENOENT     },
	{ ERROR_PATH_NOT_FOUND,         ENOENT     },
	{ ERROR_INVALID_HANDLE,         EBADF      },
	{ ERROR_ACCESS_DENIED,          EACCES     },
	{ ERROR_PRIVILEGE_NOT_HELD,     EPERM      },
	{ ERROR_NOT_ENOUGH_MEMORY,      ENOMEM     },
	{ ERROR_OUTOFMEMORY,            ENOMEM     },
	{ ERROR_INVALID_PARAMETER,      EINVAL     },
	{ ERROR_INVALID_DATA,           EINVAL     },
	{ ERROR_INSUFFICIENT_BUFFER,    ENOBUFS    },
	{ ERROR_BAD_LENGTH,             EINVAL     },
	{ ERROR_BAD_FORMAT,             EINVAL     },
	{ ERROR_HANDLE_EOF,             0          }, /* clean EOF */
	{ ERROR_BROKEN_PIPE,            EPIPE      },
	{ ERROR_NO_DATA,                EPIPE      },
	{ ERROR_PIPE_NOT_CONNECTED,     ENOTCONN   },
	{ ERROR_PIPE_BUSY,              EBUSY      },
	{ ERROR_PIPE_CONNECTED,         EISCONN    },
	{ ERROR_PIPE_LISTENING,         ENOTCONN   },
	{ ERROR_BAD_PIPE,               EINVAL     },
	{ ERROR_MORE_DATA,              EMSGSIZE   },
	{ ERROR_OPERATION_ABORTED,      ECANCELED  },
	{ ERROR_IO_PENDING,             EAGAIN     },
	{ ERROR_IO_INCOMPLETE,          EAGAIN     },
	{ ERROR_TIMEOUT,                ETIMEDOUT  },
	{ ERROR_SEM_TIMEOUT,            ETIMEDOUT  },
	{ WAIT_TIMEOUT,                 ETIMEDOUT  },
	{ ERROR_ALREADY_EXISTS,         EEXIST     },
	{ ERROR_FILE_EXISTS,            EEXIST     },
	{ ERROR_DIR_NOT_EMPTY,          ENOTEMPTY  },
	{ ERROR_DIRECTORY,              ENOTDIR    },
	{ ERROR_TOO_MANY_OPEN_FILES,    EMFILE     },
	{ ERROR_SHARING_VIOLATION,      EBUSY      },
	{ ERROR_LOCK_VIOLATION,         EBUSY      },
	{ ERROR_DISK_FULL,              ENOSPC     },
	{ ERROR_HANDLE_DISK_FULL,       ENOSPC     },
	{ ERROR_BUFFER_OVERFLOW,        ENAMETOOLONG },
	{ ERROR_FILENAME_EXCED_RANGE,   ENAMETOOLONG },
	{ ERROR_NOT_SUPPORTED,          ENOTSUP    },
	{ ERROR_CALL_NOT_IMPLEMENTED,   ENOSYS     },
	{ ERROR_PROC_NOT_FOUND,         ENOSYS     },
	{ ERROR_GEN_FAILURE,            EIO        },
	{ ERROR_CRC,                    EIO        },
	{ ERROR_BAD_NETPATH,            ENOENT     },
	{ ERROR_NETWORK_UNREACHABLE,    ENETUNREACH },
	{ ERROR_HOST_UNREACHABLE,       EHOSTUNREACH },
	{ ERROR_CONNECTION_REFUSED,     ECONNREFUSED },
	{ ERROR_CONNECTION_ABORTED,     ECONNABORTED },
	{ ERROR_NETNAME_DELETED,        ECONNRESET },
	{ ERROR_INVALID_NAME,           EINVAL     },
};

int
win32_errno_from_code(unsigned long code)
{
	size_t i;

	for (i = 0; i < sizeof win32_errno_table / sizeof *win32_errno_table;
	    i++) {
		if (win32_errno_table[i].win32_code == code)
			return (win32_errno_table[i].posix_errno);
	}
	return (EINVAL);
}

int
win32_errno_from_lasterror(void)
{
	return (win32_errno_from_code(GetLastError()));
}

#endif /* _WIN32 */
