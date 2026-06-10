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

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdint.h>

#include "win32-handle.h"

int
win32_handle_message_from_handle(void *handle, int detect_console,
    struct win32_handle_message *message)
{
	HANDLE		source = handle;
	DWORD		mode;

	if (message == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	if (source == NULL || source == INVALID_HANDLE_VALUE) {
		SetLastError(ERROR_INVALID_HANDLE);
		return (-1);
	}
	message->process_id = GetCurrentProcessId();
	message->flags = 0;
	if (detect_console && GetConsoleMode(source, &mode))
		message->flags |= WIN32_HANDLE_MESSAGE_CONSOLE;
	message->handle = (uint64_t)(uintptr_t)source;
	return (0);
}

int
win32_handle_message_from_fd(int fd, struct win32_handle_message *message)
{
	intptr_t	handle;

	handle = _get_osfhandle(fd);
	if (handle == -1) {
		SetLastError(ERROR_INVALID_HANDLE);
		return (-1);
	}
	return (win32_handle_message_from_handle((HANDLE)handle, 1, message));
}

int
win32_handle_message_to_fd(const struct win32_handle_message *message,
    int flags, DWORD expected_pid)
{
	HANDLE		process, target = NULL;
	DWORD		file_type, desired_access;
	int		fd;

	if (message == NULL || message->process_id == 0 ||
	    message->handle == 0) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}

	/* Verify the process_id matches the authenticated client PID. */
	if (expected_pid != 0 && message->process_id != expected_pid) {
		SetLastError(ERROR_ACCESS_DENIED);
		return (-1);
	}

	process = OpenProcess(PROCESS_DUP_HANDLE, FALSE, message->process_id);
	if (process == NULL)
		return (-1);

	/*
	 * First, duplicate with DUPLICATE_SAME_ACCESS so we can inspect
	 * the handle type, then we will re-duplicate with restricted access.
	 */
	if (!DuplicateHandle(process, (HANDLE)(uintptr_t)message->handle,
	    GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		CloseHandle(process);
		return (-1);
	}
	CloseHandle(process);

	/* Verify the handle type is one we allow: pipe, file, or console. */
	file_type = GetFileType(target);
	if (file_type == FILE_TYPE_UNKNOWN) {
		CloseHandle(target);
		SetLastError(ERROR_INVALID_HANDLE);
		return (-1);
	}

	/*
	 * Re-duplicate with restricted access rights based on handle type.
	 * This prevents handles with overly broad access (e.g. GENERIC_ALL,
	 * PROCESS_ALL_ACCESS, WRITE_DAC, WRITE_OWNER) from being accepted.
	 * The whitelist per handle type:
	 *   Pipe:  generic read/write, synchronize
	 *   File:  generic read/write, synchronize, read attributes
	 *   Console (char): generic read/write, synchronize
	 */
	switch (file_type) {
	case FILE_TYPE_PIPE:
		desired_access = GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE;
		break;
	case FILE_TYPE_DISK:
		desired_access = GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE |
		    FILE_READ_ATTRIBUTES;
		break;
	case FILE_TYPE_CHAR:
		desired_access = GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE;
		break;
	default:
		CloseHandle(target);
		SetLastError(ERROR_ACCESS_DENIED);
		return (-1);
	}
	{
		HANDLE	restricted = NULL;
		if (!DuplicateHandle(GetCurrentProcess(), target,
		    GetCurrentProcess(), &restricted, desired_access, FALSE,
		    0)) {
			CloseHandle(target);
			SetLastError(ERROR_ACCESS_DENIED);
			return (-1);
		}
		CloseHandle(target);
		target = restricted;
	}

	fd = _open_osfhandle((intptr_t)target, flags | _O_NOINHERIT);
	if (fd == -1) {
		CloseHandle(target);
		SetLastError(ERROR_INVALID_HANDLE);
		return (-1);
	}
	return (fd);
}

#endif
