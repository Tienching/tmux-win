/* Exercise actual Win32 process bridges without touching an existing server. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

static BOOL WINAPI tracked_close(HANDLE);
static HANDLE WINAPI tracked_thread(LPSECURITY_ATTRIBUTES, SIZE_T,
    LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);

#define CloseHandle tracked_close
#define CreateThread tracked_thread
#include "../compat/win32-process.c"
#undef CloseHandle
#undef CreateThread

static HANDLE pipes[2];
static volatile LONG closes[2];
static int fail_worker, failures;
static int track_pipes = 1;

/*
 * Keep worker pipe values allocated until the assertion. This prevents handle
 * reuse from hiding a duplicate CloseHandle (or damaging another test handle).
 * The child only echoes and exits, so retaining these parent ends is harmless.
 */
static BOOL WINAPI
tracked_close(HANDLE handle)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (handle != NULL && handle == pipes[i]) {
			InterlockedIncrement(&closes[i]);
			return (TRUE);
		}
	}
	return (CloseHandle(handle));
}

static HANDLE WINAPI
tracked_thread(LPSECURITY_ATTRIBUTES security, SIZE_T stack,
    LPTHREAD_START_ROUTINE start, LPVOID arg, DWORD flags, LPDWORD id)
{
	int worker = 0;
	if (!track_pipes)
		return (CreateThread(security, stack, start, arg, flags, id));
	if (start == win32_process_socket_to_stdin) {
		pipes[0] = ((struct win32_process_input_args *)arg)->input;
		worker = 1;
	} else if (start == win32_process_stdout_to_socket) {
		pipes[1] = ((struct win32_process_output_args *)arg)->output;
		worker = 2;
	}
	if (worker != 0 && worker == fail_worker) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (NULL);
	}
	return (CreateThread(security, stack, start, arg, flags, id));
}

static void
check(int condition, const char *name)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s (failure=%d, closes=%ld/%ld)\n",
		    name, fail_worker, closes[0], closes[1]);
		failures++;
	}
}

static void
run_eof_case(void)
{
	struct win32_process process;
	struct win32_process_options options = {0};
	uintptr_t master;
	unsigned long status = 1;
	const char *text = "TMUX_STDIN_EOF\r\n";
	char buffer[128] = {0};
	DWORD timeout = 3000;
	int n, used = 0;

	track_pipes = 0;
	options.command = L"cmd.exe /d /c findstr TMUX_STDIN_EOF";
	if (win32_process_spawn(&process, &options, &master) != 0) {
		check(0, "EOF child spawn");
		return;
	}
	setsockopt((SOCKET)master, SOL_SOCKET, SO_RCVTIMEO,
	    (const char *)&timeout, sizeof timeout);
	check(send((SOCKET)master, text, (int)strlen(text), 0) ==
	    (int)strlen(text), "send child input");
	check(win32_socket_shutdown(master, 1) == 0, "half-close child input");
	while (used < (int)sizeof buffer - 1 &&
	    (n = recv((SOCKET)master, buffer + used, sizeof buffer - 1 - used, 0)) > 0)
		used += n;
	check(strstr(buffer, "TMUX_STDIN_EOF") != NULL,
	    "stdout survives stdin EOF");
	check(win32_process_wait(&process, 3000, &status) == 1 && status == 0,
	    "worker close delivers real stdin EOF");
	win32_socket_close(master);
	check(win32_process_close(&process) == 0, "EOF workers close normally");
}

static void
run_case(int failure, int discard)
{
	struct win32_process process;
	struct win32_process_options options = {0};
	uintptr_t master;
	unsigned long status = 1;
	int result, i;

	memset(pipes, 0, sizeof pipes);
	closes[0] = closes[1] = 0;
	fail_worker = failure;
	options.command = L"cmd.exe /d /c echo TMUX_HANDLE_OWNERSHIP";
	options.discard_stdout = discard;
	options.show_stderr = 1;
	result = win32_process_spawn(&process, &options, &master);
	if (failure == 0) {
		check(result == 0, "spawn succeeds");
		if (result == 0) {
			check(process.input == NULL && process.output == NULL,
			    "successful workers own pipes exclusively");
			check(win32_process_wait(&process, 3000, &status) == 1 &&
			    status == 0, "child exits normally");
			win32_socket_close(master);
			check(win32_process_close(&process) == 0,
			    "workers finish before close returns");
		}
	} else {
		check(result == -1, "injected worker failure is reported");
		check(master == (uintptr_t)INVALID_SOCKET,
		    "failed spawn does not publish a socket");
	}
	/* Close is idempotent, including after partial-spawn failure cleanup. */
	check(win32_process_close(&process) == 0, "repeated close is safe");
	for (i = 0; i < 2; i++) {
		if (pipes[i] != NULL) {
			check(closes[i] == 1, "each pipe is closed exactly once");
			CloseHandle(pipes[i]);
			pipes[i] = NULL;
		}
	}
}

int
main(void)
{
	run_case(0, 0);
	run_case(0, 1);
	run_case(1, 0);
	run_case(2, 0);
	run_eof_case();
	if (failures != 0)
		return (1);
	puts("PASS: process pipe single ownership, discard, partial spawn failures, repeated close, real stdin EOF");
	return (0);
}
