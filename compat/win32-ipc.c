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

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"
#include "win32-command.h"
#include "win32-ipc.h"

#ifndef __unused
#ifdef __GNUC__
#define __unused __attribute__((unused))
#else
#define __unused
#endif
#endif

#define WIN32_IPC_MAGIC		"tmux-win32-ipc-v1\n"
#define WIN32_IPC_ENDPOINT_MAX	128

static INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;

struct win32_ipc_security {
	SECURITY_ATTRIBUTES	 attributes;
	SECURITY_DESCRIPTOR	 descriptor;
	ACL			*acl;
	TOKEN_USER		*user;
};

/*
 * Pending handshake object for nonblocking token validation.
 * Allocated per accepted connection that hasn't yet sent a valid token.
 */
struct win32_ipc_pending {
	SOCKET			 socket;
	struct win32_ipc_listener	*listener;
	unsigned char		 got[WIN32_IPC_TOKEN_SIZE];
	size_t			 got_len;
	struct event		 read_event;
	struct event		 timer_event;
	win32_ipc_handshake_cb	 callback;
	void			*callback_arg;
};

static BOOL CALLBACK
win32_ipc_winsock_once(__unused PINIT_ONCE once, __unused PVOID parameter,
    __unused PVOID *context)
{
	WSADATA	data;

	return (WSAStartup(MAKEWORD(2, 2), &data) == 0);
}

static int
win32_ipc_winsock_init(void)
{
	if (!InitOnceExecuteOnce(&winsock_once, win32_ipc_winsock_once, NULL,
	    NULL))
		return (-1);
	return (0);
}

static int
win32_ipc_security_init(struct win32_ipc_security *security)
{
	HANDLE	token = NULL;
	DWORD	needed = 0, acl_size;
	PSID	sid;

	memset(security, 0, sizeof *security);
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		goto fail;
	GetTokenInformation(token, TokenUser, NULL, 0, &needed);
	if (needed == 0)
		goto fail;
	security->user = malloc(needed);
	if (security->user == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto fail;
	}
	if (!GetTokenInformation(token, TokenUser, security->user, needed,
	    &needed))
		goto fail;

	sid = security->user->User.Sid;
	acl_size = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) -
	    sizeof(DWORD) + GetLengthSid(sid);
	security->acl = malloc(acl_size);
	if (security->acl == NULL) {
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		goto fail;
	}
	if (!InitializeAcl(security->acl, acl_size, ACL_REVISION))
		goto fail;
	if (!AddAccessAllowedAce(security->acl, ACL_REVISION, GENERIC_ALL,
	    sid))
		goto fail;
	if (!InitializeSecurityDescriptor(&security->descriptor,
	    SECURITY_DESCRIPTOR_REVISION))
		goto fail;
	if (!SetSecurityDescriptorDacl(&security->descriptor, TRUE,
	    security->acl, FALSE))
		goto fail;

	security->attributes.nLength = sizeof security->attributes;
	security->attributes.lpSecurityDescriptor = &security->descriptor;
	security->attributes.bInheritHandle = FALSE;

	CloseHandle(token);
	return (0);

fail:
	if (token != NULL)
		CloseHandle(token);
	free(security->acl);
	free(security->user);
	memset(security, 0, sizeof *security);
	return (-1);
}

static void
win32_ipc_security_free(struct win32_ipc_security *security)
{
	free(security->acl);
	free(security->user);
	memset(security, 0, sizeof *security);
}

static int
win32_ipc_random(unsigned char *buf, size_t len)
{
	typedef BOOLEAN (WINAPI *rtlgenrandom_fn)(PVOID, ULONG);
	HMODULE		 dll;
	rtlgenrandom_fn random;
	BOOLEAN		 ok;

	dll = LoadLibraryW(L"advapi32.dll");
	if (dll == NULL)
		return (-1);
	random = (rtlgenrandom_fn)(void *)GetProcAddress(dll,
	    "SystemFunction036");
	if (random == NULL) {
		FreeLibrary(dll);
		SetLastError(ERROR_PROC_NOT_FOUND);
		return (-1);
	}
	ok = random(buf, (ULONG)len);
	FreeLibrary(dll);
	if (!ok) {
		SetLastError(ERROR_GEN_FAILURE);
		return (-1);
	}
	return (0);
}

static void
win32_ipc_token_to_hex(const unsigned char *token, char *hex)
{
	static const char	digits[] = "0123456789abcdef";
	u_int			i;

	for (i = 0; i < WIN32_IPC_TOKEN_SIZE; i++) {
		hex[i * 2] = digits[token[i] >> 4];
		hex[i * 2 + 1] = digits[token[i] & 0x0f];
	}
	hex[WIN32_IPC_TOKEN_SIZE * 2] = '\0';
}

static int
win32_ipc_hex_value(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);
	if (ch >= 'A' && ch <= 'F')
		return (ch - 'A' + 10);
	return (-1);
}

static int
win32_ipc_token_from_hex(const char *hex, unsigned char *token)
{
	int	hi, lo;
	u_int	i;

	for (i = 0; i < WIN32_IPC_TOKEN_SIZE; i++) {
		hi = win32_ipc_hex_value(hex[i * 2]);
		lo = win32_ipc_hex_value(hex[i * 2 + 1]);
		if (hi == -1 || lo == -1)
			return (-1);
		token[i] = (unsigned char)((hi << 4) | lo);
	}
	if (hex[WIN32_IPC_TOKEN_SIZE * 2] != '\0' &&
	    hex[WIN32_IPC_TOKEN_SIZE * 2] != '\n' &&
	    hex[WIN32_IPC_TOKEN_SIZE * 2] != '\r')
		return (-1);
	return (0);
}

static int
win32_ipc_write_endpoint(const wchar_t *path, unsigned short port,
    const unsigned char *token)
{
	struct win32_ipc_security security;
	HANDLE	file = INVALID_HANDLE_VALUE;
	DWORD	written;
	char	token_hex[WIN32_IPC_TOKEN_SIZE * 2 + 1];
	char	endpoint[WIN32_IPC_ENDPOINT_MAX];
	wchar_t	*dir = NULL, *tmp = NULL;
	size_t	path_len, dir_len;
	int	n, retval = -1;

	win32_ipc_token_to_hex(token, token_hex);
	n = snprintf(endpoint, sizeof endpoint, "%s%u\n%lu\n%s\n",
	    WIN32_IPC_MAGIC, (u_int)port,
	    (unsigned long)GetCurrentProcessId(), token_hex);
	if (n <= 0 || (size_t)n >= sizeof endpoint) {
		SetLastError(ERROR_INSUFFICIENT_BUFFER);
		return (-1);
	}
	if (win32_ipc_security_init(&security) != 0)
		return (-1);

	/* Build temp file path in the same directory as the target. */
	path_len = wcslen(path);
	dir = malloc((path_len + 1) * sizeof(wchar_t));
	if (dir == NULL) {
		win32_ipc_security_free(&security);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	wcscpy(dir, path);
	for (dir_len = path_len; dir_len > 0; dir_len--) {
		if (dir[dir_len - 1] == L'\\' || dir[dir_len - 1] == L'/') {
			dir[dir_len] = L'\0';
			break;
		}
	}
	if (dir_len == 0) {
		dir[0] = L'.';
		dir[1] = L'\\';
		dir[2] = L'\0';
		dir_len = 2;
	}
	tmp = malloc((dir_len + 32) * sizeof(wchar_t));
	if (tmp == NULL) {
		win32_ipc_security_free(&security);
		free(dir);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	_snwprintf(tmp, dir_len + 32, L"%stmux-ep-%lu.tmp", dir,
	    (unsigned long)GetCurrentProcessId());

	file = CreateFileW(tmp, GENERIC_WRITE, 0, &security.attributes,
	    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	win32_ipc_security_free(&security);
	if (file == INVALID_HANDLE_VALUE)
		goto cleanup;
	if (!WriteFile(file, endpoint, (DWORD)n, &written, NULL) ||
	    written != (DWORD)n) {
		CloseHandle(file);
		DeleteFileW(tmp);
		goto cleanup;
	}
	if (!FlushFileBuffers(file)) {
		CloseHandle(file);
		DeleteFileW(tmp);
		goto cleanup;
	}
	CloseHandle(file);
	file = INVALID_HANDLE_VALUE;

	if (!MoveFileExW(tmp, path,
	    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		goto cleanup;

	retval = 0;

cleanup:
	if (retval != 0 && tmp != NULL)
		DeleteFileW(tmp);
	free(tmp);
	free(dir);
	return (retval);
}

static int
win32_ipc_read_endpoint(const wchar_t *path, unsigned short *port,
    unsigned char *token)
{
	HANDLE		file, process;
	LARGE_INTEGER	size;
	DWORD		read;
	char		*buffer, *end, *pid_end;
	unsigned long	value, pid;
	size_t		magic_len = strlen(WIN32_IPC_MAGIC);
	int		retval = -1;

	file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
	    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return (-1);
	if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
	    size.QuadPart >= 4096) {
		CloseHandle(file);
		SetLastError(ERROR_BAD_FORMAT);
		return (-1);
	}
	buffer = malloc((size_t)size.QuadPart + 1);
	if (buffer == NULL) {
		CloseHandle(file);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return (-1);
	}
	if (!ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL)) {
		free(buffer);
		CloseHandle(file);
		return (-1);
	}
	CloseHandle(file);
	buffer[read] = '\0';

	if (strncmp(buffer, WIN32_IPC_MAGIC, magic_len) != 0)
		goto bad_format;
	value = strtoul(buffer + magic_len, &end, 10);
	if (end == buffer + magic_len || value == 0 || value > 65535 ||
	    (*end != '\n' && *end != '\r' && *end != '\0'))
		goto bad_format;
	while (*end == '\n' || *end == '\r')
		end++;

	/* Parse PID field. */
	pid = strtoul(end, &pid_end, 10);
	if (pid_end == end || pid == 0 ||
	    (*pid_end != '\n' && *pid_end != '\r' && *pid_end != '\0'))
		goto bad_format;
	while (*pid_end == '\n' || *pid_end == '\r')
		pid_end++;

	if (win32_ipc_token_from_hex(pid_end, token) != 0)
		goto bad_format;

	/* Verify the PID is still alive. */
	process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
	    (DWORD)pid);
	if (process == NULL) {
		SetLastError(ERROR_NOT_FOUND);
		free(buffer);
		return (-1);
	}
	CloseHandle(process);

	*port = (unsigned short)value;
	retval = 0;
	goto out;

bad_format:
	SetLastError(ERROR_BAD_FORMAT);
out:
	free(buffer);
	return (retval);
}

static int
win32_ipc_send_all(SOCKET socket, const unsigned char *buf, size_t len)
{
	int	n;

	while (len != 0) {
		n = send(socket, (const char *)buf, (int)len, 0);
		if (n <= 0)
			return (-1);
		buf += n;
		len -= (size_t)n;
	}
	return (0);
}

static int
win32_ipc_recv_all(SOCKET socket, unsigned char *buf, size_t len)
{
	int	n;

	while (len != 0) {
		n = recv(socket, (char *)buf, (int)len, 0);
		if (n <= 0)
			return (-1);
		buf += n;
		len -= (size_t)n;
	}
	return (0);
}

int
win32_ipc_listen(const char *path, struct win32_ipc_listener *listener)
{
	struct sockaddr_in	addr;
	SOCKET			sock = INVALID_SOCKET;
	int			len, on = 1;
	wchar_t			*wpath = NULL;

	if (path == NULL || listener == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	memset(listener, 0, sizeof *listener);
	listener->socket = (uintptr_t)INVALID_SOCKET;

	if (win32_ipc_winsock_init() != 0)
		return (-1);
	wpath = win32_utf8_to_wide(path);
	if (wpath == NULL)
		return (-1);
	if (win32_ipc_random(listener->token, sizeof listener->token) != 0)
		goto fail;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
		goto fail;
	setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
	    (const char *)&on, sizeof on);

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(sock, (struct sockaddr *)&addr, sizeof addr) ==
	    SOCKET_ERROR)
		goto fail;
	if (listen(sock, 128) == SOCKET_ERROR)
		goto fail;

	len = sizeof addr;
	if (getsockname(sock, (struct sockaddr *)&addr, &len) ==
	    SOCKET_ERROR)
		goto fail;

	listener->socket = (uintptr_t)sock;
	listener->path = wpath;
	listener->port = ntohs(addr.sin_port);
	sock = INVALID_SOCKET;
	wpath = NULL;

	if (win32_ipc_write_endpoint(listener->path, listener->port,
	    listener->token) != 0)
		goto fail_listener;
	return (0);

fail_listener:
	win32_ipc_listener_close(listener);
	return (-1);

fail:
	if (sock != INVALID_SOCKET)
		closesocket(sock);
	free(wpath);
	memset(listener, 0, sizeof *listener);
	listener->socket = (uintptr_t)INVALID_SOCKET;
	return (-1);
}

/*
 * Free a pending handshake object and close its socket if still open.
 */
static void
win32_ipc_pending_free(struct win32_ipc_pending *pending)
{
	if (pending == NULL)
		return;
	if (event_initialized(&pending->read_event))
		event_del(&pending->read_event);
	if (event_initialized(&pending->timer_event))
		event_del(&pending->timer_event);
	if (pending->socket != INVALID_SOCKET)
		closesocket(pending->socket);
	free(pending);
}

/*
 * libevent read callback: called when data is available on the
 * pending socket.  Reads available bytes, appends to the token
 * buffer, and validates when the full token has been received.
 */
static void
win32_ipc_pending_read_cb(__unused evutil_socket_t fd,
    __unused short events, void *arg)
{
	struct win32_ipc_pending	*pending = arg;
	int				 n;
	size_t				 remaining;
	uintptr_t			 validated_socket;
	win32_ipc_handshake_cb	 callback;
	void			*callback_arg;

	remaining = WIN32_IPC_TOKEN_SIZE - pending->got_len;
	n = recv(pending->socket,
	    (char *)(pending->got + pending->got_len),
	    (int)remaining, 0);
	if (n <= 0) {
		/* Connection closed or error before token complete. */
		callback = pending->callback;
		callback_arg = pending->callback_arg;
		win32_ipc_pending_free(pending);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}
	pending->got_len += (size_t)n;

	if (pending->got_len < WIN32_IPC_TOKEN_SIZE)
		return; /* Need more data. */

	/* Full token received — validate. */
	if (memcmp(pending->got, pending->listener->token,
	    WIN32_IPC_TOKEN_SIZE) != 0) {
		callback = pending->callback;
		callback_arg = pending->callback_arg;
		win32_ipc_pending_free(pending);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}

	/* Token valid — deliver the socket. */
	validated_socket = (uintptr_t)pending->socket;
	pending->socket = INVALID_SOCKET; /* Prevent double close. */
	callback = pending->callback;
	callback_arg = pending->callback_arg;
	win32_ipc_pending_free(pending);
	callback(validated_socket, callback_arg);
}

/*
 * libevent timer callback: called when the handshake times out.
 */
static void
win32_ipc_pending_timer_cb(__unused evutil_socket_t fd,
    __unused short events, void *arg)
{
	struct win32_ipc_pending	*pending = arg;
	win32_ipc_handshake_cb		 callback;
	void			*callback_arg;

	callback = pending->callback;
	callback_arg = pending->callback_arg;
	win32_ipc_pending_free(pending);
	callback((uintptr_t)INVALID_SOCKET, callback_arg);
}

/*
 * Nonblocking accept: accept a connection, set it nonblocking,
 * register a libevent read event for token validation, and a
 * timeout event.  Returns immediately.  The callback is invoked
 * when the handshake succeeds or fails.
 *
 * Uses event_set() to work with the global event base that tmux
 * uses via the deprecated event_loop() API.
 */
void
win32_ipc_accept_nonblocking(struct win32_ipc_listener *listener,
    win32_ipc_handshake_cb callback, void *callback_arg)
{
	struct win32_ipc_pending	*pending = NULL;
	SOCKET				 sock;
	u_long				 mode;
	struct timeval			 tv;

	sock = accept((SOCKET)listener->socket, NULL, NULL);
	if (sock == INVALID_SOCKET) {
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}

	/* Set nonblocking immediately. */
	mode = 1;
	if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
		closesocket(sock);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}

	pending = calloc(1, sizeof *pending);
	if (pending == NULL) {
		closesocket(sock);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}
	pending->socket = sock;
	pending->listener = listener;
	pending->got_len = 0;
	pending->callback = callback;
	pending->callback_arg = callback_arg;

	/* Register read event for token validation. */
	event_set(&pending->read_event, (evutil_socket_t)sock,
	    EV_READ | EV_PERSIST, win32_ipc_pending_read_cb, pending);
	if (event_add(&pending->read_event, NULL) != 0) {
		win32_ipc_pending_free(pending);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}

	/* Register timeout event. */
	tv.tv_sec = WIN32_IPC_HANDSHAKE_TIMEOUT_SEC;
	tv.tv_usec = 0;
	evtimer_set(&pending->timer_event, win32_ipc_pending_timer_cb,
	    pending);
	if (event_add(&pending->timer_event, &tv) != 0) {
		win32_ipc_pending_free(pending);
		callback((uintptr_t)INVALID_SOCKET, callback_arg);
		return;
	}
}

/*
 * Legacy blocking accept — kept for compatibility but should not
 * be used in the server event loop.  Still used by some test paths.
 */
int
win32_ipc_accept(struct win32_ipc_listener *listener, uintptr_t *socket_out)
{
	SOCKET		sock;
	unsigned char	token[WIN32_IPC_TOKEN_SIZE];
	DWORD		timeout = 5000, no_timeout = 0;
	u_long		mode;

	if (listener == NULL || socket_out == NULL ||
	    listener->socket == (uintptr_t)INVALID_SOCKET) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*socket_out = (uintptr_t)INVALID_SOCKET;

	sock = accept((SOCKET)listener->socket, NULL, NULL);
	if (sock == INVALID_SOCKET)
		return (-1);
	mode = 0;
	if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
		closesocket(sock);
		return (-1);
	}
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
	    sizeof timeout);
	if (win32_ipc_recv_all(sock, token, sizeof token) != 0) {
		closesocket(sock);
		return (-1);
	}
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&no_timeout,
	    sizeof no_timeout);
	if (memcmp(token, listener->token, sizeof token) != 0) {
		closesocket(sock);
		WSASetLastError(WSAEACCES);
		return (-1);
	}
	mode = 1;
	if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
		closesocket(sock);
		return (-1);
	}
	*socket_out = (uintptr_t)sock;
	return (0);
}

int
win32_ipc_connect(const char *path, uintptr_t *socket_out)
{
	struct sockaddr_in	addr;
	SOCKET			sock = INVALID_SOCKET;
	wchar_t			*wpath = NULL;
	unsigned char		token[WIN32_IPC_TOKEN_SIZE];
	unsigned short		port;
	u_long			mode;

	if (path == NULL || socket_out == NULL) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return (-1);
	}
	*socket_out = (uintptr_t)INVALID_SOCKET;

	if (win32_ipc_winsock_init() != 0)
		return (-1);
	wpath = win32_utf8_to_wide(path);
	if (wpath == NULL)
		return (-1);
	if (win32_ipc_read_endpoint(wpath, &port, token) != 0)
		goto fail;

	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET)
		goto fail;

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);
	if (connect(sock, (struct sockaddr *)&addr, sizeof addr) ==
	    SOCKET_ERROR)
		goto fail;
	if (win32_ipc_send_all(sock, token, sizeof token) != 0)
		goto fail;
	mode = 1;
	if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR)
		goto fail;

	free(wpath);
	*socket_out = (uintptr_t)sock;
	return (0);

fail:
	if (sock != INVALID_SOCKET)
		closesocket(sock);
	free(wpath);
	return (-1);
}

void
win32_ipc_listener_close(struct win32_ipc_listener *listener)
{
	if (listener == NULL)
		return;
	if (listener->socket != (uintptr_t)INVALID_SOCKET)
		closesocket((SOCKET)listener->socket);
	if (listener->path != NULL) {
		DeleteFileW(listener->path);
		free(listener->path);
	}
	memset(listener, 0, sizeof *listener);
	listener->socket = (uintptr_t)INVALID_SOCKET;
}

#endif
