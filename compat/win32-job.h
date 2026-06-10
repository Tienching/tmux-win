#ifndef TMUX_WIN32_JOB_H
#define TMUX_WIN32_JOB_H

#ifdef _WIN32

int	win32_job_current_process_in_job(void);
DWORD	win32_job_creation_flags_for_child(void);
int	win32_job_assign_or_fallback(HANDLE job, HANDLE process);

#endif
#endif
