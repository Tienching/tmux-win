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

#include <string.h>
#include <windows.h>

#include "win32-job.h"

int
win32_job_current_process_in_job(void)
{
	BOOL	result;

	if (!IsProcessInJob(GetCurrentProcess(), NULL, &result))
		return (0);
	return (result != 0);
}

DWORD
win32_job_creation_flags_for_child(void)
{
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION	info;

	if (!win32_job_current_process_in_job())
		return (0);

	memset(&info, 0, sizeof info);
	if (!QueryInformationJobObject(NULL, JobObjectExtendedLimitInformation,
	    &info, sizeof info, NULL))
		return (0);

	/*
	 * CREATE_BREAKAWAY_FROM_JOB fails with ERROR_ACCESS_DENIED unless the
	 * current job explicitly permits breakaway. Task Scheduler commonly
	 * places logon tasks in a job without that flag, so requesting breakaway
	 * merely because we are in a job prevents tmux from creating any child.
	 * SILENT_BREAKAWAY_OK needs no creation flag.
	 */
	if (info.BasicLimitInformation.LimitFlags &
	    JOB_OBJECT_LIMIT_BREAKAWAY_OK)
		return (CREATE_BREAKAWAY_FROM_JOB);
	return (0);
}

enum win32_job_assign_result
win32_job_assign_or_fallback(HANDLE job, HANDLE process)
{
	if (AssignProcessToJobObject(job, process))
		return (WIN32_JOB_ASSIGN_OK);

	if (GetLastError() == ERROR_ACCESS_DENIED) {
		/*
		 * The process is already in a non-breakaway job that
		 * prevents assignment to our job.  The caller must close
		 * the local job handle and set job=NULL so that terminate
		 * paths fall back to process-tree + TerminateProcess instead
		 * of TerminateJobObject (which would only terminate an
		 * empty job).
		 */
		return (WIN32_JOB_ASSIGN_PARENT_JOB);
	}
	return (WIN32_JOB_ASSIGN_FAILED);
}

#endif
