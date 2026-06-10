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

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <string.h>

#include "win32-process-tree.h"

/*
 * Compare two FILETIME values.  Returns negative if a < b, zero if equal,
 * positive if a > b.
 */
static int
win32_process_tree_compare_filetime(const FILETIME *a, const FILETIME *b)
{
	ULARGE_INTEGER	ua, ub;

	ua.LowPart = a->dwLowDateTime;
	ua.HighPart = a->dwHighDateTime;
	ub.LowPart = b->dwLowDateTime;
	ub.HighPart = b->dwHighDateTime;
	if (ua.QuadPart < ub.QuadPart)
		return (-1);
	if (ua.QuadPart > ub.QuadPart)
		return (1);
	return (0);
}

/*
 * Recursively terminate child processes of the given parent PID.
 * Validates creation times against the root process creation time to
 * avoid killing processes whose PID was reused after the root started.
 * visited/visited_count prevents infinite recursion in cyclic trees.
 */
static void
win32_process_tree_terminate_recursive(HANDLE snapshot,
    DWORD parent_id, const FILETIME *root_creation,
    DWORD *visited, unsigned int *visited_count, unsigned int exit_code)
{
	PROCESSENTRY32W	entry;
	FILETIME	candidate_creation, ftExit, ftKernel, ftUser;
	HANDLE		process;
	unsigned int	i;

	memset(&entry, 0, sizeof entry);
	entry.dwSize = sizeof entry;
	if (!Process32FirstW(snapshot, &entry))
		return;

	do {
		if (entry.th32ParentProcessID != parent_id)
			continue;

		/* Check visited to prevent recursion loops. */
		for (i = 0; i < *visited_count; i++) {
			if (visited[i] == entry.th32ProcessID)
				break;
		}
		if (i < *visited_count)
			continue;

		/* Open process for query and terminate. */
		process = OpenProcess(
		    PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
		    FALSE, entry.th32ProcessID);
		if (process == NULL)
			continue;

		/* Validate creation time against root. */
		if (!GetProcessTimes(process, &candidate_creation, &ftExit,
		    &ftKernel, &ftUser) ||
		    win32_process_tree_compare_filetime(&candidate_creation,
		    root_creation) < 0) {
			CloseHandle(process);
			continue;
		}

		/* Mark as visited before recursing. */
		if (*visited_count < 256)
			visited[(*visited_count)++] = entry.th32ProcessID;

		/* Recurse into children first. */
		win32_process_tree_terminate_recursive(snapshot,
		    entry.th32ProcessID, root_creation, visited, visited_count,
		    exit_code);

		/* Terminate this process. */
		TerminateProcess(process, exit_code);
		CloseHandle(process);
	} while (Process32NextW(snapshot, &entry));
}

void
win32_process_tree_terminate_children(DWORD root_pid, HANDLE root_process,
    unsigned int exit_code)
{
	HANDLE		snapshot;
	FILETIME	root_creation, ftExit, ftKernel, ftUser;
	DWORD		visited[256];
	unsigned int	visited_count = 0;

	if (!GetProcessTimes(root_process, &root_creation, &ftExit,
	    &ftKernel, &ftUser))
		return;

	snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	visited[visited_count++] = root_pid;
	win32_process_tree_terminate_recursive(snapshot, root_pid,
	    &root_creation, visited, &visited_count, exit_code);

	CloseHandle(snapshot);
}

#endif
