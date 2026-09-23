/* Real libevent/Winsock fault injection: only sockets owned by this process. */
#define HAVE_EVENT2_EVENT_H 1
#define free test_free
#include "../proc.c"
#undef free
#include <assert.h>

static int dropped, delivered, timed_out;
static ULONGLONG expires;
void test_free(void *ptr) { (void)ptr; }
void log_debug(const char *fmt, ...) { (void)fmt; }
int win32_socket_pending(uintptr_t fd, unsigned long *n) {
    return ioctlsocket((SOCKET)fd, FIONREAD, n);
}
int win32_socket_close(uintptr_t fd) { return closesocket((SOCKET)fd); }
void imsgbuf_clear(struct imsgbuf *b) { (void)b; }
static void lost(struct imsg *m, void *arg) {
    assert(m == NULL); dropped++; proc_remove_peer(arg);
}
static void readable(evutil_socket_t fd, short events, void *arg) {
    char b; (void)events; (void)arg;
    assert(recv(fd, &b, 1, 0) == 1 && b == 'X'); delivered++;
}
static void deadline(evutil_socket_t fd, short events, void *arg) {
    (void)fd; (void)events; (void)arg; timed_out=1;
}
static int done(void) {
    if (GetTickCount64() >= expires) timed_out=1;
    return timed_out || (dropped == 1 && delivered == 1);
}
static void socket_pair(SOCKET *s) {
    struct sockaddr_in addr = {0}; int len=sizeof addr;
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    assert(listener!=INVALID_SOCKET);
    addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(struct sockaddr *)&addr,len)==0);
    assert(getsockname(listener,(struct sockaddr *)&addr,&len)==0);
    assert(listen(listener,1)==0);
    s[0]=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    assert(connect(s[0],(struct sockaddr *)&addr,len)==0);
    s[1]=accept(listener,NULL,NULL); assert(s[1]!=INVALID_SOCKET);
    closesocket(listener);
}
int main(void) {
    WSADATA wsa; struct tmuxproc tp; struct tmuxpeer peer;
    struct event good, timeout; struct timeval tv={2,0};
    SOCKET bad_pair[2],good_pair[2];
    assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    assert(event_init()!=NULL);
    socket_pair(bad_pair); socket_pair(good_pair);
    memset(&tp,0,sizeof tp); memset(&peer,0,sizeof peer);
    tp.name="integration"; TAILQ_INIT(&tp.peers);
    peer.parent=&tp; peer.ibuf.fd=bad_pair[0]; peer.dispatchcb=lost; peer.arg=&peer;
    TAILQ_INSERT_TAIL(&tp.peers,&peer,entry);
    event_set(&peer.event,bad_pair[0],EV_READ|EV_PERSIST,readable,NULL);
    evtimer_set(&peer.poll_event,deadline,NULL);
    assert(event_add(&peer.event,NULL)==0);
    event_set(&good,good_pair[0],EV_READ|EV_PERSIST,readable,NULL);
    assert(event_add(&good,NULL)==0);
    evtimer_set(&timeout,deadline,NULL); evtimer_add(&timeout,&tv);
    /* Reproduce the live failure: closed socket retained in the select set. */
    closesocket(bad_pair[0]);
    assert(send(good_pair[1],"X",1,0)==1);
    assert(event_loop(EVLOOP_NONBLOCK)==-1);
    assert(WSAGetLastError()==WSAENOTSOCK);
    expires=GetTickCount64()+2000;
    proc_loop(&tp,done);
    assert(!timed_out && dropped==1 && delivered==1);
    assert(TAILQ_EMPTY(&tp.peers));
    assert(peer.ibuf.fd==(imsg_fd_t)INVALID_SOCKET);
    event_del(&good); event_del(&timeout);
    closesocket(bad_pair[1]); closesocket(good_pair[0]); closesocket(good_pair[1]);
    WSACleanup();
    puts("PASS real Winsock/libevent invalid watcher recovered; healthy socket delivered");
    return 0;
}
