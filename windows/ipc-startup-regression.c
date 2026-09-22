/* Deterministic failures against the real client/IPC implementations. */
#define HAVE_EVENT2_EVENT_H 1
#include "tmux.h"
#include <assert.h>
static void noisy_free(void *);
static void fake_sleep(DWORD);
static HANDLE WINAPI query_process(DWORD, BOOL, DWORD);
static BOOL WINAPI query_exit(HANDLE, LPDWORD);
static BOOL WINAPI create_process(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
    LPSTARTUPINFOW, LPPROCESS_INFORMATION);
static int test_connect(const char *, uintptr_t *);
#define free noisy_free
#define Sleep fake_sleep
#define win32_ipc_connect test_connect
#define CreateProcessW create_process
#ifdef BASELINE_CLIENT_SOURCE
#include BASELINE_CLIENT_SOURCE
#else
#include "../client.c"
#endif
#undef CreateProcessW
#undef win32_ipc_connect
#define OpenProcess query_process
#define GetExitCodeProcess query_exit
#ifdef BASELINE_IPC_SOURCE
#include BASELINE_IPC_SOURCE
#else
#include "../compat/win32-ipc.c"
#endif
#undef OpenProcess
#undef GetExitCodeProcess
#undef Sleep
#undef free
#include "../compat/win32-command.c"

static DWORD open_error, exit_error, connect_error, slept;
static int failures, calls, create_calls;
char **cfg_files;
u_int cfg_nfiles;
int cfg_user_files;
void *xcalloc(size_t n, size_t s) { void *p=calloc(n,s); assert(p); return p; }
int log_get_level(void) { return 0; }
void log_debug(const char *fmt, ...) { (void)fmt; SetLastError(999); }
unsigned long win32_job_creation_flags_for_child(void) { return 0; }
int win32_socket_set_blocking(uintptr_t fd, int b) { (void)fd; (void)b; return 0; }
static void noisy_free(void *p) { free(p); SetLastError(999); }
static void fake_sleep(DWORD ms) { slept+=ms; SetLastError(999); }
static HANDLE WINAPI query_process(DWORD access, BOOL inherit, DWORD pid) {
    if (open_error) { SetLastError(open_error); return NULL; }
    return OpenProcess(access,inherit,pid);
}
static BOOL WINAPI query_exit(HANDLE p, LPDWORD code) {
    if (exit_error) { SetLastError(exit_error); return FALSE; }
    return GetExitCodeProcess(p,code);
}
static BOOL WINAPI create_process(LPCWSTR a, LPWSTR b, LPSECURITY_ATTRIBUTES c,
    LPSECURITY_ATTRIBUTES d, BOOL e, DWORD f, LPVOID g, LPCWSTR h,
    LPSTARTUPINFOW i, LPPROCESS_INFORMATION j) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;
    create_calls++; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
static int test_connect(const char *path, uintptr_t *fd) {
    calls++;
    if (failures-- > 0) { SetLastError(connect_error); return -1; }
    return win32_ipc_connect(path,fd);
}
static int retry(const char *path, uintptr_t *fd) {
#ifdef BASELINE_CLIENT_SOURCE
    return client_win32_retry_connect(path,fd);
#else
    return client_win32_retry_connect(path,fd,GetCurrentProcess());
#endif
}
int main(int argc, char **argv) {
    char dir[MAX_PATH], path[MAX_PATH], missing[MAX_PATH], data[256];
    wchar_t *wide; DWORD written; HANDLE file, hold, process=NULL;
    SOCKET listener; struct sockaddr_in addr={0}; int length=sizeof addr;
    uintptr_t fd; WSADATA wsa;
    if (argc==2 && strcmp(argv[1],"--exit-fixture")==0) return 42;
    assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    assert(GetTempPathA(sizeof dir,dir)>0);
    assert(GetTempFileNameA(dir,"tmi",0,path)!=0);
    snprintf(missing,sizeof missing,"%s.absent",path);
    assert(win32_ipc_connect(missing,&fd)==-1);
    assert(GetLastError()==ERROR_FILE_NOT_FOUND);
    puts("PASS missing endpoint error survives free and logging");
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); assert(listener!=INVALID_SOCKET);
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(struct sockaddr *)&addr,sizeof addr)==0);
    assert(getsockname(listener,(struct sockaddr *)&addr,&length)==0);
    assert(listen(listener,8)==0);
    snprintf(data,sizeof data,"tmux-win32-ipc-v1\n%u\n%lu\n%064d\n",
        ntohs(addr.sin_port),GetCurrentProcessId(),0);
    file=CreateFileA(path,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
    assert(file!=INVALID_HANDLE_VALUE);
    assert(WriteFile(file,data,(DWORD)strlen(data),&written,NULL)); CloseHandle(file);
    assert(win32_ipc_endpoint_owner_alive(path)==1);
    open_error=ERROR_ACCESS_DENIED;
    assert(win32_ipc_endpoint_owner_alive(path)==-1);
    open_error=ERROR_NOT_ENOUGH_MEMORY;
    assert(win32_ipc_endpoint_owner_alive(path)==-1);
    open_error=ERROR_INVALID_PARAMETER;
    assert(win32_ipc_endpoint_owner_alive(path)==0);
    open_error=0; exit_error=ERROR_ACCESS_DENIED;
    assert(win32_ipc_endpoint_owner_alive(path)==-1); exit_error=0;
    puts("PASS unreadable owner remains untrusted, not stale");
    wide=win32_utf8_to_wide(path); assert(wide);
    hold=CreateFileW(wide,DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,OPEN_EXISTING,0,NULL); assert(hold!=INVALID_HANDLE_VALUE);
    assert(win32_ipc_connect(path,&fd)==0); closesocket((SOCKET)fd);
    CloseHandle(hold); free(wide);
    puts("PASS endpoint reader coexists with delete-sharing handle");
    failures=2; connect_error=ERROR_SHARING_VIOLATION; calls=0; slept=0;
    assert(retry(path,&fd)==0); assert(calls==3 && slept==200);
    closesocket((SOCKET)fd);
    failures=2; connect_error=ERROR_LOCK_VIOLATION; calls=0; slept=0;
    assert(retry(path,&fd)==0); assert(calls==3 && slept==200);
    closesocket((SOCKET)fd);
    puts("PASS transient locks retry on the same server only");
    failures=100; connect_error=ERROR_FILE_NOT_FOUND; calls=0; slept=0;
    assert(retry(path,&fd)==-1); assert(calls==50 && slept==5000);
    assert(GetLastError()==ERROR_FILE_NOT_FOUND);
    failures=100; connect_error=ERROR_ACCESS_DENIED; calls=0; slept=0;
    assert(retry(path,&fd)==-1); assert(calls==1 && slept==0);
    assert(GetLastError()==ERROR_ACCESS_DENIED);
    assert(create_calls==0);
    puts("PASS bounded wait and fail-closed permission denial");
#ifdef BASELINE_CLIENT_SOURCE
    assert(client_win32_start_server(path)==-1);
#else
    assert(client_win32_start_server(path,&process)==-1); assert(process==NULL);
#endif
    assert(create_calls==1 && GetLastError()==ERROR_ACCESS_DENIED);
    puts("PASS process creation error survives cleanup");
#ifndef BASELINE_CLIENT_SOURCE
    {
        wchar_t module[MAX_PATH], command[MAX_PATH+40];
        STARTUPINFOW si={0}; PROCESS_INFORMATION pi={0}; DWORD code;
        assert(GetModuleFileNameW(NULL,module,MAX_PATH)>0);
        swprintf(command,MAX_PATH+40,L"\"%ls\" --exit-fixture",module);
        si.cb=sizeof si;
        assert(CreateProcessW(module,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,
            NULL,NULL,&si,&pi));
        CloseHandle(pi.hThread);
        assert(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0);
        assert(GetExitCodeProcess(pi.hProcess,&code) && code==42);
        calls=0; slept=0;
        assert(client_win32_retry_connect(path,&fd,pi.hProcess)==-1);
        assert(GetLastError()==ERROR_PROCESS_ABORTED && calls==0 && slept==0);
        assert(CloseHandle(pi.hProcess));
        puts("PASS exited server stops startup retries immediately (code 42)");
    }
#endif
    closesocket(listener); DeleteFileA(path); WSACleanup();
    puts("Windows IPC startup regression passed"); return 0;
}
