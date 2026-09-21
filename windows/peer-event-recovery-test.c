/* Exercise the actual proc.c implementation with deterministic I/O failures. */
#include <winsock2.h>
#include <windows.h>
static void test_sleep(DWORD);
void test_free(void *);
#define Sleep test_sleep
#define HAVE_EVENT2_EVENT_H 1
#define free test_free
#define event_add test_event_add
#define event_del test_event_del
#define event_loop test_event_loop
#ifdef BASELINE_SOURCE
#include BASELINE_SOURCE
#else
#include "../proc.c"
#endif
#undef free
#undef Sleep
#include <assert.h>

static struct tmuxpeer *victim;
static int timer_armed, freed, after_free, closed_invalid, drops, leave_linked;
static int invalid_fd = 41, pending_bytes = 1, loop_result = -1;
static int pending_error;
static int added, deleted, loop_calls;
static int sleep_calls;
static DWORD slept_ms;

static void test_sleep(DWORD ms) { sleep_calls++; slept_ms+=ms; }

void test_free(void *p) { if (p == victim) freed = 1; }
int test_event_add(struct event *e, const struct timeval *tv) {
    (void)tv; added++;
    if (victim && e == &victim->poll_event) {
        if (freed) after_free++;
        timer_armed = 1;
    }
    return 0;
}
int test_event_del(struct event *e) {
    deleted++;
    if (victim && e == &victim->poll_event) timer_armed = 0;
    return 0;
}
int test_event_loop(int flags) { (void)flags; loop_calls++; return loop_result; }
void log_debug(const char *fmt, ...) { (void)fmt; }
int win32_socket_pending(uintptr_t fd, unsigned long *n) {
    if (pending_error) { WSASetLastError(pending_error); return -1; }
    if (fd == (uintptr_t)invalid_fd || fd == (uintptr_t)INVALID_SOCKET) {
        WSASetLastError(WSAENOTSOCK); return -1;
    }
    *n = pending_bytes; return 0;
}
int win32_socket_close(uintptr_t fd) {
    if (fd == (uintptr_t)invalid_fd) closed_invalid++;
    return 0;
}
int imsgbuf_read(struct imsgbuf *b) { (void)b; return 0; }
int imsgbuf_write(struct imsgbuf *b) { (void)b; return 0; }
void imsgbuf_clear(struct imsgbuf *b) { (void)b; }
uint32_t imsgbuf_queuelen(struct imsgbuf *b) { (void)b; return 0; }
ssize_t imsg_get(struct imsgbuf *b, struct imsg *m) { (void)b;(void)m; return 0; }
void imsg_free(struct imsg *m) { (void)m; }
int imsg_compose(struct imsgbuf *b, uint32_t type, uint32_t peerid, pid_t pid,
    int fd, const void *data, size_t len) {
    (void)b;(void)type;(void)peerid;(void)pid;(void)fd;(void)data;(void)len; return 1;
}
static void lost(struct imsg *m, void *arg) {
    assert(m == NULL); drops++;
    if (!leave_linked) proc_remove_peer(arg);
}
static int stop_loop(void) { return 1; }
static void setup(struct tmuxproc *tp, struct tmuxpeer *p, int fd) {
    memset(tp, 0, sizeof *tp); memset(p, 0, sizeof *p);
    tp->name = "test"; TAILQ_INIT(&tp->peers);
    p->parent=tp; p->ibuf.fd=fd; p->dispatchcb=lost; p->arg=p;
    TAILQ_INSERT_TAIL(&tp->peers,p,entry);
    victim=p; timer_armed=freed=after_free=closed_invalid=drops=leave_linked=0;
    added=deleted=loop_calls=pending_error=sleep_calls=0; slept_ms=0; pending_bytes=1;
}
int main(void) {
    struct tmuxproc tp;
    struct tmuxpeer p, good;
    setup(&tp,&p,80);
    proc_win32_poll_cb(0,0,&p);
    assert(drops==1 && freed && !after_free && !timer_armed);
    puts("PASS poll callback teardown never rearms freed peer");
#ifndef BASELINE_SOURCE
    setup(&tp,&p,41);
    proc_win32_poll_cb(0,0,&p);
    assert(drops==1 && freed && !closed_invalid && !timer_armed);
    puts("PASS invalid poll socket retired without closing stale handle");
    setup(&tp,&p,41); memset(&good,0,sizeof good);
    good.parent=&tp; good.ibuf.fd=80; TAILQ_INSERT_TAIL(&tp.peers,&good,entry);
    assert(proc_win32_recover_peers(&tp)==1);
    assert(drops==1 && !closed_invalid && TAILQ_FIRST(&tp.peers)==&good);
    puts("PASS recovery preserves healthy peer");
    setup(&tp,&p,41); leave_linked=1;
    assert(proc_win32_recover_peers(&tp)==1);
    assert(drops==1 && deleted==2 && p.ibuf.fd==(imsg_fd_t)INVALID_SOCKET);
    assert(proc_send(&p,MSG_VERSION,-1,NULL,0)==-1 && deleted==2);
    puts("PASS non-removing callback quarantined without loop");
    setup(&tp,&p,80); pending_bytes=0;
    proc_win32_poll_cb(0,0,&p);
    assert(!drops && timer_armed && !freed);
    puts("PASS healthy idle peer remains scheduled");
    setup(&tp,&p,80); loop_result=-1;
    proc_loop(&tp,stop_loop);
    assert(loop_calls==1 && sleep_calls==1 && slept_ms==10 && !drops);
    puts("PASS unknown loop failure backs off preserving healthy peer");
    setup(&tp,&p,80); pending_error=WSAEINTR;
    proc_win32_poll_cb(0,0,&p);
    assert(!drops && timer_armed && !freed && p.ibuf.fd==80);
    puts("PASS inconclusive poll failure does not disconnect healthy peer");
    setup(&tp,&p,80); pending_error=WSAEINTR;
    assert(proc_win32_recover_peers(&tp)==0);
    assert(!drops && !deleted && p.ibuf.fd==80);
    puts("PASS recovery only quarantines confirmed invalid sockets");
    puts("8 lifecycle regression checks passed");
#endif
    return 0;
}
