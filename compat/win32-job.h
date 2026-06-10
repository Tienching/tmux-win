#ifndef TMUX_WIN32_JOB_H
#define TMUX_WIN32_JOB_H

#ifdef _WIN32

/*
 * Result of win32_job_assign_or_fallback():
 *
 *   WIN32_JOB_ASSIGN_OK          - Process was assigned to the job.
 *   WIN32_JOB_ASSIGN_PARENT_JOB  - Assignment failed because the process is
 *                                  already in a non-breakaway parent job.
 *                                  The caller should close the local job
 *                                  handle and set job=NULL so that terminate
 *                                  paths use process-tree + TerminateProcess
 *                                  instead of TerminateJobObject.
 *   WIN32_JOB_ASSIGN_FAILED      - Unexpected error.
 */
enum win32_job_assign_result {
	WIN32_JOB_ASSIGN_OK = 0,
	WIN32_JOB_ASSIGN_PARENT_JOB = 1,
	WIN32_JOB_ASSIGN_FAILED = -1
};

int	win32_job_current_process_in_job(void);
DWORD	win32_job_creation_flags_for_child(void);
enum win32_job_assign_result	win32_job_assign_or_fallback(HANDLE job,
	    HANDLE process);

#endif
#endif
