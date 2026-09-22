/* Standalone, isolated fault injector. Link win32-conpty/job/process-tree. */
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include "../compat/win32-conpty.h"
struct payload_result { wchar_t cwd[MAX_PATH], environment[128], argument[128]; };

int wmain(int argc, wchar_t **argv)
{
	struct win32_conpty pty;
	wchar_t exe[32768], command[32768], directory[MAX_PATH], marker[MAX_PATH];
	PROCESS_INFORMATION child = {0};
	STARTUPINFOW startup = {sizeof startup};
	HANDLE file, process;
	DWORD bytes, pid, error, status;
	ULONGLONG started, elapsed;
	struct payload_result payload = {0};
	wchar_t workspace[MAX_PATH];
	if (argc == 4 && wcscmp(argv[1], L"--payload") == 0) {
		GetCurrentDirectoryW(MAX_PATH, payload.cwd);
		GetEnvironmentVariableW(L"TMUX_BOOTSTRAP_TEST", payload.environment, 128);
		wcsncpy(payload.argument, argv[3], 127);
		file = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ, NULL,
		    CREATE_ALWAYS, 0, NULL);
		if (file == INVALID_HANDLE_VALUE) return 13;
		WriteFile(file, &payload, sizeof payload, &bytes, NULL);
		CloseHandle(file);
		return 37;
	}
	if (argc > 1 && wcscmp(argv[1], L"--win32-conpty-child") == 0) {
		/* Deliberately create a child but never send its startup ACK. */
		if (argc == 5 && wcscmp(argv[3], L"lost-ack") == 0) {
			GetModuleFileNameW(NULL, exe, 32768);
			swprintf(command, 32768, L"\"%ls\" --sleep-child", exe);
			if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0,
			    NULL, NULL, &startup, &child)) return 2;
			file = CreateFileW(argv[4], GENERIC_WRITE, FILE_SHARE_READ,
			    NULL, CREATE_ALWAYS, 0, NULL);
			if (file == INVALID_HANDLE_VALUE) return 3;
			WriteFile(file, &child.dwProcessId, sizeof(DWORD), &bytes, NULL);
			CloseHandle(file);
			CloseHandle(child.hThread);
			CloseHandle(child.hProcess);
			Sleep(INFINITE);
			return 4;
		}
		return win32_conpty_child_main();
	}
	if (argc > 1 && wcscmp(argv[1], L"--sleep-child") == 0) {
		Sleep(INFINITE);
		return 5;
	}
	GetTempPathW(MAX_PATH, directory);
	GetTempFileNameW(directory, L"ack", 0, marker);
	swprintf(command, 32768, L"lost-ack \"%ls\"", marker);
	started = GetTickCount64();
	if (win32_conpty_spawn(&pty, command, NULL, NULL, 80, 24) == 0) {
		win32_conpty_close(&pty);
		return 6;
	}
	error = GetLastError();
	elapsed = GetTickCount64() - started;
	file = CreateFileW(marker, GENERIC_READ, FILE_SHARE_READ, NULL,
	    OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE) return 7;
	if (!ReadFile(file, &pid, sizeof pid, &bytes, NULL) || bytes != sizeof pid)
		return 8;
	CloseHandle(file);
	DeleteFileW(marker);
	process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
	    FALSE, pid);
	if (process != NULL) {
		status = WaitForSingleObject(process, 2000);
		CloseHandle(process);
		if (status != WAIT_OBJECT_0) return 9;
	}
	if (error != ERROR_TIMEOUT || elapsed > 8000) return 10;
	printf("lost_ack: timeout=%lu elapsed_ms=%llu child_terminated=true\n",
	    error, (unsigned long long)elapsed);
	if (win32_conpty_spawn(&pty, L"C:\\no-such-tmux-test-program.exe",
	    NULL, NULL, 80, 24) == 0) {
		win32_conpty_close(&pty);
		return 11;
	}
	if (GetLastError() != ERROR_FILE_NOT_FOUND) return 12;
	puts("missing_command: original_error_2_preserved=true");
	swprintf(workspace, MAX_PATH, L"%ls-unicode 测试", marker);
	if (!CreateDirectoryW(workspace, NULL)) return 14;
	SetEnvironmentVariableW(L"TMUX_BOOTSTRAP_TEST", L"env value 测试");
	GetModuleFileNameW(NULL, exe, 32768);
	swprintf(command, 32768, L"\"%ls\" --payload \"%ls\" \"quoted value 测试\"", exe, marker);
	if (win32_conpty_spawn(&pty, command, workspace, NULL, 80, 24) != 0) return 15;
	if (WaitForSingleObject((HANDLE)pty.process, 5000) != WAIT_OBJECT_0) return 16;
	GetExitCodeProcess((HANDLE)pty.process, &status);
	win32_conpty_close(&pty);
	file = CreateFileW(marker, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE) return 17;
	ReadFile(file, &payload, sizeof payload, &bytes, NULL);
	CloseHandle(file);
	DeleteFileW(marker);
	RemoveDirectoryW(workspace);
	if (bytes != sizeof payload || status != 37 ||
	    _wcsicmp(payload.cwd, workspace) != 0 ||
	    wcscmp(payload.environment, L"env value 测试") != 0 ||
	    wcscmp(payload.argument, L"quoted value 测试") != 0) return 18;
	puts("native_startup: unicode_cwd_environment_quoting_exit37=true");
	return 0;
}
