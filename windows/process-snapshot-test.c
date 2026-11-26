/* Test the actual native cache implementation without touching any server. */
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <assert.h>
#include <stdio.h>
static ULONGLONG clock_ms;
static int opened, closed, fail_snapshot;
static size_t index_entry;
static const struct {
    DWORD pid, parent, creation;
    const wchar_t *name;
} processes[] = {
    { 10, 1, 200, L"shell.exe" },
    { 30, 10, 100, L"csrss.exe" }, /* Parent PID was reused. */
    { 40, 10, 201, L"conhost.exe" },
    { 20, 10, 300, L"child.exe" },
    { 50, 20, 250, L"unrelated.exe" }, /* Newer than the root, older than its alleged parent. */
};
static ULONGLONG fake_ticks(void) { return clock_ms; }
static HANDLE fake_snapshot(DWORD flags, DWORD pid) {
    assert(flags == TH32CS_SNAPPROCESS && pid == 0);
    opened++; clock_ms += 120; /* slower than the format's 100 ms budget */
    return fail_snapshot ? INVALID_HANDLE_VALUE : (HANDLE)(uintptr_t)opened;
}
static BOOL fake_close(HANDLE handle) {
    assert(handle != INVALID_HANDLE_VALUE);
    if ((uintptr_t)handle < 1000) closed++;
    return TRUE;
}
static HANDLE fake_open(DWORD access, BOOL inherit, DWORD pid) {
    assert(access == PROCESS_QUERY_LIMITED_INFORMATION && !inherit);
    for (size_t i = 0; i < _countof(processes); i++) {
        if (processes[i].pid == pid) return (HANDLE)(uintptr_t)(1000 + i);
    }
    return NULL;
}
static BOOL fake_process_times(HANDLE process, LPFILETIME creation,
    LPFILETIME exit, LPFILETIME kernel, LPFILETIME user) {
    uintptr_t index = (uintptr_t)process - 1000;
    assert(index < _countof(processes));
    (void)exit; (void)kernel; (void)user;
    creation->dwHighDateTime = 0;
    creation->dwLowDateTime = processes[index].creation;
    return TRUE;
}
static BOOL copy_entry(LPPROCESSENTRY32W entry) {
    if (index_entry >= _countof(processes)) return FALSE;
    entry->th32ProcessID = processes[index_entry].pid;
    entry->th32ParentProcessID = processes[index_entry].parent;
    wcscpy(entry->szExeFile, processes[index_entry].name);
    return TRUE;
}
static BOOL fake_first(HANDLE handle, LPPROCESSENTRY32W entry) {
    (void)handle; index_entry = 0;
    return copy_entry(entry);
}
static BOOL fake_next(HANDLE handle, LPPROCESSENTRY32W entry) {
    (void)handle; index_entry++;
    return copy_entry(entry);
}
#define GetTickCount64 fake_ticks
#define CreateToolhelp32Snapshot fake_snapshot
#define CloseHandle fake_close
#define OpenProcess fake_open
#define GetProcessTimes fake_process_times
#define Process32FirstW fake_first
#define Process32NextW fake_next
#include "../osdep-windows.c"

int main(void) {
    osdep_format_begin();
    assert(opened == 1 && clock_ms == 120);
    for (int i = 0; i < 18; i++) assert(osdep_win32_active_pid(10) == 20);
    assert(opened == 1); /* one snapshot, not two scans per pane */
    clock_ms += 1000;
    osdep_format_begin(); /* nested expansion cannot replace the snapshot */
    assert(osdep_win32_active_pid(10) == 20 && opened == 1);
    osdep_format_end();
    osdep_format_end();
    osdep_format_begin();
    assert(opened == 2 && closed == 1);
    osdep_format_end();
    clock_ms += 499;
    assert(osdep_win32_active_pid(10) == 20 && opened == 2);
    clock_ms++;
    assert(osdep_win32_active_pid(10) == 20 && opened == 3 && closed == 2);
    clock_ms += 500; fail_snapshot = 1;
    osdep_format_begin();
    assert(osdep_win32_active_pid(10) == 10 && opened == 4 && closed == 3);
    osdep_format_end();
    assert(osdep_win32_active_pid(10) == 10 && opened == 4);
    clock_ms += 500; fail_snapshot = 0;
    assert(osdep_win32_active_pid(10) == 20 && opened == 5 && closed == 3);
    puts("PASS: snapshot reuse, expiry, nesting, slow acquisition, failure backoff and recovery");
    puts("PASS: reject snapshot children created before their claimed parent");
    return 0;
}
