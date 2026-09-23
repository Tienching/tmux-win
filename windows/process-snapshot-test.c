/* Test the actual native cache implementation without touching any server. */
#include <winsock2.h>
#include <windows.h>
#include <tlhelp32.h>
#include <assert.h>
#include <stdio.h>
static ULONGLONG clock_ms;
static int opened, closed, fail_snapshot, index_entry;
static ULONGLONG fake_ticks(void) { return clock_ms; }
static HANDLE fake_snapshot(DWORD flags, DWORD pid) {
    assert(flags == TH32CS_SNAPPROCESS && pid == 0);
    opened++; clock_ms += 120; /* slower than the format's 100 ms budget */
    return fail_snapshot ? INVALID_HANDLE_VALUE : (HANDLE)(uintptr_t)opened;
}
static BOOL fake_close(HANDLE handle) {
    assert(handle != INVALID_HANDLE_VALUE); closed++; return TRUE;
}
static BOOL fake_first(HANDLE handle, LPPROCESSENTRY32W entry) {
    (void)handle; index_entry = 0;
    entry->th32ProcessID = 10; entry->th32ParentProcessID = 1;
    wcscpy(entry->szExeFile, L"shell.exe"); return TRUE;
}
static BOOL fake_next(HANDLE handle, LPPROCESSENTRY32W entry) {
    (void)handle;
    if (index_entry++ != 0) return FALSE;
    entry->th32ProcessID = 20; entry->th32ParentProcessID = 10;
    wcscpy(entry->szExeFile, L"child.exe"); return TRUE;
}
#define GetTickCount64 fake_ticks
#define CreateToolhelp32Snapshot fake_snapshot
#define CloseHandle fake_close
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
    return 0;
}
