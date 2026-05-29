param(
	[string]$CC = $(if ($env:CC) { $env:CC } else { "gcc" }),
	[string]$CXX = $(if ($env:CXX) { $env:CXX } else { "g++" }),
	[string]$Yacc = $(if ($env:YACC) { $env:YACC } else { "" }),
	[string]$Version = $(if ($env:TMUX_VERSION) { $env:TMUX_VERSION } else { "probe" }),
	[string]$OutputExe = "",
	[switch]$UseGeneratedParser,
	[switch]$UseSystemLibevent,
	[string]$LibeventCflags = "",
	[string]$LibeventLibs = "",
	[switch]$KeepTemp
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $false

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Temp = Join-Path $Root ".codex-tmp\mingw-probe"

function ConvertTo-CStringLiteral([string]$Text) {
	$escaped = $Text.Replace('\', '\\').Replace('"', '\"')
	$escaped = $escaped.Replace("`r", "\r").Replace("`n", "\n")
	return '"' + $escaped + '"'
}

function New-ProbeDirectory {
	if (Test-Path -LiteralPath $Temp) {
		$resolved = (Resolve-Path -LiteralPath $Temp).Path
		$prefix = $Root + [System.IO.Path]::DirectorySeparatorChar
		if (-not $resolved.StartsWith($prefix)) {
			throw "refusing to remove outside workspace: $resolved"
		}
		Remove-Item -LiteralPath $resolved -Recurse -Force
	}
	New-Item -ItemType Directory -Force -Path $Temp | Out-Null
}

function Write-ProbeFiles {
	$versionLiteral = ConvertTo-CStringLiteral $Version
	Set-Content -LiteralPath (Join-Path $Temp "event.h") -Encoding ascii -Value @'
#ifndef TMUX_PROBE_EVENT_H
#define TMUX_PROBE_EVENT_H
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct timeval;
struct bufferevent;
typedef intptr_t evutil_socket_t;
typedef void (*event_callback_fn)(evutil_socket_t, short, void *);
typedef void (*event_log_cb)(int, const char *);
typedef void (*bufferevent_data_cb)(struct bufferevent *, void *);
typedef void (*bufferevent_event_cb)(struct bufferevent *, short, void *);
struct event_base { int dummy; };
struct event { int dummy; };
struct evbuffer { unsigned char *data; size_t len; };
struct bufferevent {
	struct event ev_read;
	struct event ev_write;
	struct evbuffer *input;
	struct evbuffer *output;
	short enabled;
};
struct event_base *event_init(void);
void event_set_log_callback(event_log_cb);
void event_set(struct event *, int, short, event_callback_fn, void *);
int event_base_set(struct event_base *, struct event *);
int event_add(struct event *, const struct timeval *);
void event_active(struct event *, int, short);
const char *event_get_version(void);
const char *event_get_method(void);
int event_loop(int);
int event_reinit(struct event_base *);
int event_initialized(const struct event *);
int event_del(struct event *);
int event_pending(const struct event *, short, struct timeval *);
int event_once(evutil_socket_t, short, event_callback_fn, void *,
    const struct timeval *);
int evtimer_add(struct event *, const struct timeval *);
int evtimer_del(struct event *);
int evtimer_initialized(const struct event *);
int evtimer_pending(const struct event *, struct timeval *);
size_t evbuffer_get_length(const struct evbuffer *);
unsigned char *evbuffer_pullup(struct evbuffer *, ssize_t);
void evbuffer_drain(struct evbuffer *, size_t);
char *evbuffer_readln(struct evbuffer *, size_t *, int);
char *evbuffer_readline(struct evbuffer *);
struct evbuffer *evbuffer_new(void);
void evbuffer_free(struct evbuffer *);
int evbuffer_add(struct evbuffer *, const void *, size_t);
int evbuffer_add_printf(struct evbuffer *, const char *, ...);
int evbuffer_add_vprintf(struct evbuffer *, const char *, va_list);
int evbuffer_read(struct evbuffer *, int, int);
int evbuffer_write(struct evbuffer *, int);
int evbuffer_write_atmost(struct evbuffer *, int, ssize_t);
int evbuffer_freeze(struct evbuffer *, int);
int evbuffer_unfreeze(struct evbuffer *, int);
struct bufferevent *bufferevent_new(evutil_socket_t, bufferevent_data_cb,
    bufferevent_data_cb, bufferevent_event_cb, void *);
int bufferevent_enable(struct bufferevent *, short);
int bufferevent_disable(struct bufferevent *, short);
int bufferevent_write(struct bufferevent *, const void *, size_t);
int bufferevent_write_buffer(struct bufferevent *, struct evbuffer *);
void bufferevent_setwatermark(struct bufferevent *, short, size_t, size_t);
void bufferevent_free(struct bufferevent *);
#define EV_READ 0x01
#define EV_WRITE 0x02
#define EV_TIMEOUT 0x04
#define EV_PERSIST 0x10
#define EVLOOP_ONCE 0x01
#define EVBUFFER_ERROR 0x20
#define EVBUFFER_EOL_LF 1
#define EVBUFFER_LENGTH(b) evbuffer_get_length(b)
#define EVBUFFER_DATA(b) ((b)->data)
#define EVBUFFER_INPUT(b) ((b)->input)
#define EVBUFFER_OUTPUT(b) ((b)->output)
#define evtimer_set(ev, cb, arg) event_set((ev), -1, 0, (cb), (arg))
#endif
'@

	Set-Content -LiteralPath (Join-Path $Temp "event_stub.c") -Encoding ascii -Value @'
#include "event.h"
#include <stdlib.h>
void event_set_log_callback(event_log_cb cb) { (void)cb; }
struct event_base *event_init(void) { return calloc(1, sizeof(struct event_base)); }
void event_set(struct event *e, int fd, short events, event_callback_fn cb, void *arg)
{ (void)e; (void)fd; (void)events; (void)cb; (void)arg; }
int event_base_set(struct event_base *base, struct event *e)
{ (void)base; (void)e; return 0; }
int event_add(struct event *e, const struct timeval *tv)
{ (void)e; (void)tv; return 0; }
void event_active(struct event *e, int events, short ncalls)
{ (void)e; (void)events; (void)ncalls; }
const char *event_get_version(void) { return "probe"; }
const char *event_get_method(void) { return "probe"; }
int event_loop(int flags) { (void)flags; return 0; }
int event_reinit(struct event_base *base) { (void)base; return 0; }
int event_initialized(const struct event *e) { (void)e; return 1; }
int event_del(struct event *e) { (void)e; return 0; }
int event_pending(const struct event *e, short events, struct timeval *tv)
{ (void)e; (void)events; (void)tv; return 0; }
int event_once(evutil_socket_t fd, short events, event_callback_fn cb,
    void *arg, const struct timeval *tv)
{ (void)fd; (void)events; (void)cb; (void)arg; (void)tv; return 0; }
int evtimer_add(struct event *e, const struct timeval *tv)
{ return event_add(e, tv); }
int evtimer_del(struct event *e) { return event_del(e); }
int evtimer_initialized(const struct event *e) { return event_initialized(e); }
int evtimer_pending(const struct event *e, struct timeval *tv)
{ (void)e; (void)tv; return 0; }
struct evbuffer *evbuffer_new(void) { return calloc(1, sizeof(struct evbuffer)); }
void evbuffer_free(struct evbuffer *b)
{ if (b != NULL) { free(b->data); free(b); } }
size_t evbuffer_get_length(const struct evbuffer *b)
{ return b == NULL ? 0 : b->len; }
unsigned char *evbuffer_pullup(struct evbuffer *b, ssize_t len)
{ (void)len; return b == NULL ? NULL : b->data; }
void evbuffer_drain(struct evbuffer *b, size_t len)
{ if (b != NULL) b->len = len >= b->len ? 0 : b->len - len; }
char *evbuffer_readln(struct evbuffer *b, size_t *n, int eol)
{ (void)b; (void)eol; if (n != NULL) *n = 0; return NULL; }
char *evbuffer_readline(struct evbuffer *b)
{ (void)b; return NULL; }
int evbuffer_add(struct evbuffer *b, const void *data, size_t len)
{ (void)b; (void)data; (void)len; return 0; }
int evbuffer_add_printf(struct evbuffer *b, const char *fmt, ...)
{ (void)b; (void)fmt; return 0; }
int evbuffer_add_vprintf(struct evbuffer *b, const char *fmt, va_list ap)
{ (void)b; (void)fmt; (void)ap; return 0; }
int evbuffer_read(struct evbuffer *b, int fd, int howmuch)
{ (void)b; (void)fd; (void)howmuch; return 0; }
int evbuffer_write(struct evbuffer *b, int fd)
{ (void)b; (void)fd; return 0; }
int evbuffer_write_atmost(struct evbuffer *b, int fd, ssize_t howmuch)
{ (void)howmuch; return evbuffer_write(b, fd); }
int evbuffer_freeze(struct evbuffer *b, int at_front)
{ (void)b; (void)at_front; return 0; }
int evbuffer_unfreeze(struct evbuffer *b, int at_front)
{ (void)b; (void)at_front; return 0; }
struct bufferevent *bufferevent_new(evutil_socket_t fd, bufferevent_data_cb rcb,
    bufferevent_data_cb wcb, bufferevent_event_cb ecb, void *arg)
{
	struct bufferevent *bev;
	(void)fd; (void)rcb; (void)wcb; (void)ecb; (void)arg;
	bev = calloc(1, sizeof *bev);
	if (bev != NULL) {
		bev->input = evbuffer_new();
		bev->output = evbuffer_new();
	}
	return bev;
}
int bufferevent_enable(struct bufferevent *bev, short events)
{ (void)bev; (void)events; return 0; }
int bufferevent_disable(struct bufferevent *bev, short events)
{ (void)bev; (void)events; return 0; }
int bufferevent_write(struct bufferevent *bev, const void *data, size_t size)
{ (void)bev; (void)data; (void)size; return 0; }
int bufferevent_write_buffer(struct bufferevent *bev, struct evbuffer *buf)
{ (void)bev; (void)buf; return 0; }
void bufferevent_setwatermark(struct bufferevent *bev, short events,
    size_t lowmark, size_t highmark)
{ (void)bev; (void)events; (void)lowmark; (void)highmark; }
void bufferevent_free(struct bufferevent *bev)
{ if (bev != NULL) { evbuffer_free(bev->input); evbuffer_free(bev->output); free(bev); } }
'@

	Set-Content -LiteralPath (Join-Path $Temp "probe_config.h") -Encoding ascii -Value @"
#ifndef TMUX_VERSION
#define TMUX_VERSION $versionLiteral
#endif
"@

	Set-Content -LiteralPath (Join-Path $Temp "parser_stub.c") -Encoding ascii -Value @'
#include "tmux.h"
static struct cmd_parse_result cmd_parse_stub_result = { CMD_PARSE_ERROR, NULL, NULL };
struct cmd_parse_result *cmd_parse_from_file(FILE *f, struct cmd_parse_input *pi)
{ (void)f; (void)pi; return (&cmd_parse_stub_result); }
struct cmd_parse_result *cmd_parse_from_string(const char *s, struct cmd_parse_input *pi)
{ (void)s; (void)pi; return (&cmd_parse_stub_result); }
enum cmd_parse_status cmd_parse_and_insert(const char *s, struct cmd_parse_input *pi,
    struct cmdq_item *item, struct cmdq_state *state, char **cause)
{ (void)s; (void)pi; (void)item; (void)state; if (cause != NULL) *cause = NULL; return (CMD_PARSE_ERROR); }
enum cmd_parse_status cmd_parse_and_append(const char *s, struct cmd_parse_input *pi,
    struct client *c, struct cmdq_state *state, char **cause)
{ (void)s; (void)pi; (void)c; (void)state; if (cause != NULL) *cause = NULL; return (CMD_PARSE_ERROR); }
struct cmd_parse_result *cmd_parse_from_buffer(const void *buf, size_t len,
    struct cmd_parse_input *pi)
{ (void)buf; (void)len; (void)pi; return (&cmd_parse_stub_result); }
struct cmd_parse_result *cmd_parse_from_arguments(struct args_value *values,
    u_int count, struct cmd_parse_input *pi)
{ (void)values; (void)count; (void)pi; return (&cmd_parse_stub_result); }
'@

	Set-Content -LiteralPath (Join-Path $Temp "terminal_format_support.c") -Encoding ascii -Value @'
#include "tmux.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
fatalx(const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(2);
}

void *
xmalloc(size_t size)
{
	void	*ptr;

	if (size == 0)
		fatalx("xmalloc: zero size");
	ptr = malloc(size);
	if (ptr == NULL)
		fatalx("xmalloc: failed");
	return (ptr);
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void	*ptr;

	if (nmemb == 0 || size == 0)
		fatalx("xcalloc: zero size");
	ptr = calloc(nmemb, size);
	if (ptr == NULL)
		fatalx("xcalloc: failed");
	return (ptr);
}

void *
xrealloc(void *ptr, size_t size)
{
	void	*newptr;

	if (size == 0)
		fatalx("xrealloc: zero size");
	newptr = realloc(ptr, size);
	if (newptr == NULL)
		fatalx("xrealloc: failed");
	return (newptr);
}

void *
xreallocarray(void *ptr, size_t nmemb, size_t size)
{
	if (nmemb != 0 && size > ((size_t)-1) / nmemb)
		fatalx("xreallocarray: overflow");
	return (xrealloc(ptr, nmemb * size));
}

void *
xrecallocarray(void *ptr, size_t oldnmemb, size_t nmemb, size_t size)
{
	void	*newptr;
	size_t	 oldsize = oldnmemb * size, newsize = nmemb * size;

	newptr = xcalloc(nmemb, size);
	if (ptr != NULL) {
		memcpy(newptr, ptr, oldsize < newsize ? oldsize : newsize);
		free(ptr);
	}
	return (newptr);
}

char *
xstrdup(const char *s)
{
	char	*out;

	out = strdup(s);
	if (out == NULL)
		fatalx("xstrdup: failed");
	return (out);
}

char *
xstrndup(const char *s, size_t maxlen)
{
	char	*out;
	size_t	 len = 0;

	while (len < maxlen && s[len] != '\0')
		len++;
	out = xmalloc(len + 1);
	memcpy(out, s, len);
	out[len] = '\0';
	return (out);
}

int
xvsnprintf(char *str, size_t len, const char *fmt, va_list ap)
{
	int	n;

	n = vsnprintf(str, len, fmt, ap);
	if (n < 0 || (size_t)n >= len)
		fatalx("xvsnprintf: failed");
	return (n);
}

int
xsnprintf(char *str, size_t len, const char *fmt, ...)
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = xvsnprintf(str, len, fmt, ap);
	va_end(ap);
	return (n);
}

int
xvasprintf(char **ret, const char *fmt, va_list ap)
{
	va_list aq;
	int	n;

	va_copy(aq, ap);
	n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	if (n < 0)
		fatalx("xvasprintf: failed");
	*ret = xmalloc((size_t)n + 1);
	if (vsnprintf(*ret, (size_t)n + 1, fmt, ap) != n)
		fatalx("xvasprintf: write failed");
	return (n);
}

int
xasprintf(char **ret, const char *fmt, ...)
{
	va_list	ap;
	int	n;

	va_start(ap, fmt);
	n = xvasprintf(ret, fmt, ap);
	va_end(ap);
	return (n);
}
'@

	Set-Content -LiteralPath (Join-Path $Temp "terminal_format_probe.c") -Encoding ascii -Value @'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TMUX_TERMINAL_FORMAT_PROBE
#include "tty-term.c"

static int
check_string(const char *name, const char *got, const char *want)
{
	if (strcmp(got, want) == 0)
		return (0);
	fprintf(stderr, "%s: got %zu bytes, wanted %zu bytes\n",
	    name, strlen(got), strlen(want));
	return (1);
}

int
main(void)
{
	struct tty_term_win32_value	args[3];
	char				large[600];
	const char			*got;
	size_t				 expected;

	args[0] = tty_term_win32_number(4);
	args[1] = tty_term_win32_number(7);
	got = tty_term_win32_expand(TTYC_CUP, "\033[%d;%dH", args, 2);
	if (check_string("native indexed cup", got, "\033[5;8H") != 0)
		return (1);

	args[0] = tty_term_win32_number(10);
	args[1] = tty_term_win32_number(20);
	got = tty_term_win32_expand(TTYC_CMG, "\033[%i%p1%d;%p2%ds",
	    args, 2);
	if (check_string("terminfo indexed margin", got, "\033[11;21s") != 0)
		return (1);

	args[0] = tty_term_win32_string("c");
	args[1] = tty_term_win32_string("payload");
	got = tty_term_win32_expand(TTYC_MS, "\033]52;%p1%s;%p2%s\007",
	    args, 2);
	if (check_string("terminfo strings", got, "\033]52;c;payload\007") != 0)
		return (1);

	args[0] = tty_term_win32_number(1);
	args[1] = tty_term_win32_number(2);
	args[2] = tty_term_win32_number(3);
	got = tty_term_win32_expand(TTYC_SETRGBF,
	    "\033[38;2;%p1%d;%p2%d;%p3%dm", args, 3);
	if (check_string("terminfo rgb", got, "\033[38;2;1;2;3m") != 0)
		return (1);

	args[0] = tty_term_win32_number(3);
	got = tty_term_win32_expand(TTYC_SETAF,
	    "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m",
	    args, 1);
	if (check_string("terminfo conditional low colour", got,
	    "\033[33m") != 0)
		return (1);

	args[0] = tty_term_win32_number(12);
	got = tty_term_win32_expand(TTYC_SETAF,
	    "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m",
	    args, 1);
	if (check_string("terminfo conditional bright colour", got,
	    "\033[94m") != 0)
		return (1);

	args[0] = tty_term_win32_number(123);
	got = tty_term_win32_expand(TTYC_SETAF,
	    "\033[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m",
	    args, 1);
	if (check_string("terminfo conditional palette colour", got,
	    "\033[38;5;123m") != 0)
		return (1);

	args[0] = tty_term_win32_string("id");
	args[1] = tty_term_win32_string("https://example.invalid");
	got = tty_term_win32_expand(TTYC_HLS,
	    "\033]8;%?%p1%l%tid=%p1%s%;;%p2%s\033\\", args, 2);
	if (check_string("terminfo string length conditional", got,
	    "\033]8;id=id;https://example.invalid\033\\") != 0)
		return (1);

	memset(large, 'A', sizeof large - 1);
	large[sizeof large - 1] = '\0';
	args[0] = tty_term_win32_string("c");
	args[1] = tty_term_win32_string(large);
	got = tty_term_win32_expand(TTYC_MS, "\033]52;%p1%s;%p2%s\007",
	    args, 2);
	expected = strlen(large) + (sizeof "\033]52;c;\007" - 1);
	if (strlen(got) != expected || got[expected - 1] != '\007') {
		fprintf(stderr, "large osc52: got %zu bytes, wanted %zu bytes\n",
		    strlen(got), expected);
		return (1);
	}

	puts("terminal format probe ok");
	return (0);
}
'@
}

function Convert-ToObjectName([string]$Source, [string]$Prefix) {
	$name = $Source -replace '[:\\/\. -]', '_'
	return (Join-Path $Temp "$Prefix$name.o")
}

function Convert-ToResponseFileArgument([string]$Argument) {
	$escaped = ($Argument -replace "\\", "/").Replace('"', '\"')
	return '"' + $escaped + '"'
}

function Write-ResponseFile([string]$Path, [string[]]$Arguments) {
	$lines = $Arguments | ForEach-Object {
		Convert-ToResponseFileArgument $_
	}
	Set-Content -LiteralPath $Path -Encoding ascii -Value $lines
}

function Invoke-NativeCapture([string]$Program, [string[]]$Arguments,
    [string]$Name) {
	$stderrPath = Join-Path $Temp "$Name.stderr"
	$oldErrorActionPreference = $ErrorActionPreference
	if (Test-Path -LiteralPath $stderrPath) {
		Remove-Item -LiteralPath $stderrPath -Force
	}
	$ErrorActionPreference = "Continue"
	try {
		$stdout = & $Program @Arguments 2> $stderrPath
		$exitCode = $LASTEXITCODE
	} finally {
		$ErrorActionPreference = $oldErrorActionPreference
	}
	if (Test-Path -LiteralPath $stderrPath) {
		$stderr = Get-Content -LiteralPath $stderrPath
		Remove-Item -LiteralPath $stderrPath -Force
	} else {
		$stderr = @()
	}
	return [pscustomobject]@{
		ExitCode = $exitCode
		Output = @($stdout) + @($stderr)
	}
}

function Invoke-Compile([string]$Compiler, [string[]]$Arguments) {
	$result = Invoke-NativeCapture $Compiler $Arguments "compile"
	$output = $result.Output
	if ($result.ExitCode -ne 0) {
		$output | Select-Object -First 120 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "compile failed: $Compiler $($Arguments -join ' ')"
	}
	$diagnostics = $output | Select-String -Pattern "implicit declaration|fatal error|error:"
	if ($diagnostics) {
		$diagnostics | Select-Object -First 80 | ForEach-Object { Write-Warning $_ }
	}
}

function Split-ArgumentString([string]$Text) {
	if ([string]::IsNullOrWhiteSpace($Text)) {
		return @()
	}
	return @($Text -split '\s+' | Where-Object { $_ -ne "" })
}

function Find-Yacc {
	if (-not [string]::IsNullOrWhiteSpace($Yacc)) {
		return $Yacc
	}
	foreach ($candidate in @("bison", "win_bison", "byacc", "yacc")) {
		$cmd = Get-Command $candidate -ErrorAction SilentlyContinue
		if ($cmd -ne $null) {
			return $cmd.Source
		}
	}
	return ""
}

function Resolve-CommandPath([string]$Command) {
	if ([string]::IsNullOrWhiteSpace($Command)) {
		return ""
	}
	if ([System.IO.Path]::IsPathRooted($Command) -and
	    (Test-Path -LiteralPath $Command)) {
		return (Resolve-Path -LiteralPath $Command).Path
	}
	$cmd = Get-Command $Command -ErrorAction SilentlyContinue
	if ($cmd -ne $null -and -not [string]::IsNullOrWhiteSpace($cmd.Source)) {
		return $cmd.Source
	}
	return ""
}

function Convert-ToGccPath([string]$Path) {
	return ($Path -replace "\\", "/")
}

function Get-Msys2Prefixes {
	$prefixes = New-Object System.Collections.ArrayList
	$systems = @("mingw64", "ucrt64", "clang64", "clangarm64", "mingw32")

	function Add-Prefix([string]$Path) {
		if ([string]::IsNullOrWhiteSpace($Path) -or
		    -not (Test-Path -LiteralPath $Path)) {
			return
		}
		$resolved = (Resolve-Path -LiteralPath $Path).Path
		foreach ($existing in $prefixes) {
			if ($existing -ieq $resolved) {
				return
			}
		}
		[void]$prefixes.Add($resolved)
	}

	$ccPath = Resolve-CommandPath $CC
	if (-not [string]::IsNullOrWhiteSpace($ccPath)) {
		$bin = Split-Path -Parent $ccPath
		if ((Split-Path -Leaf $bin) -ieq "bin") {
			Add-Prefix (Split-Path -Parent $bin)
		}
	}

	foreach ($name in @("MSYSTEM_PREFIX", "MINGW_PREFIX")) {
		$value = [Environment]::GetEnvironmentVariable($name)
		if ([string]::IsNullOrWhiteSpace($value)) {
			continue
		}
		if ([System.IO.Path]::IsPathRooted($value)) {
			Add-Prefix $value
		} elseif ($value -match "^/([^/]+)$") {
			foreach ($root in @("C:\msys64", "D:\msys64")) {
				Add-Prefix (Join-Path $root $Matches[1])
			}
		}
	}

	foreach ($entry in ($env:PATH -split ";")) {
		if ([string]::IsNullOrWhiteSpace($entry) -or
		    -not (Test-Path -LiteralPath $entry)) {
			continue
		}
		if ((Split-Path -Leaf $entry) -ieq "bin") {
			$parent = Split-Path -Parent $entry
			if ($systems -contains (Split-Path -Leaf $parent).ToLowerInvariant()) {
				Add-Prefix $parent
			}
		}
	}

	foreach ($root in @("C:\msys64", "D:\msys64")) {
		foreach ($system in $systems) {
			Add-Prefix (Join-Path $root $system)
		}
	}

	return @($prefixes)
}

function Find-Msys2PrefixByName([string]$Name) {
	foreach ($prefix in Get-Msys2Prefixes) {
		if ((Split-Path -Leaf $prefix) -ieq $Name) {
			return $prefix
		}
	}
	return ""
}

function Convert-Msys2PkgConfigFlags([string[]]$Flags) {
	$result = @()
	foreach ($flag in $Flags) {
		if ($flag -match "^(-[IL])/([^/]+)(/.*)?$") {
			$prefix = Find-Msys2PrefixByName $Matches[2]
			if (-not [string]::IsNullOrWhiteSpace($prefix)) {
				$path = $prefix
				if ($Matches[3]) {
					$rest = $Matches[3].TrimStart("/") -replace "/",
					    [System.IO.Path]::DirectorySeparatorChar
					$path = Join-Path $prefix $rest
				}
				$result += $Matches[1] + (Convert-ToGccPath $path)
				continue
			}
		}
		$result += $flag
	}
	return @($result)
}

function Add-LibeventBinPathsFromFlags($Result) {
	foreach ($prefix in Get-Msys2Prefixes) {
		$libFlag = "-L" + (Convert-ToGccPath (Join-Path $prefix "lib"))
		if ($Result.Libs -contains $libFlag) {
			$bin = Join-Path $prefix "bin"
			if ($Result.BinPaths -notcontains $bin) {
				$Result.BinPaths += $bin
			}
		}
	}
}

function Find-Msys2LibeventFlags {
	foreach ($prefix in Get-Msys2Prefixes) {
		$include = Join-Path $prefix "include"
		$header = Join-Path $include "event2\event.h"
		$lib = Join-Path $prefix "lib\libevent.dll.a"
		$staticLib = Join-Path $prefix "lib\libevent.a"
		if ((Test-Path -LiteralPath $header) -and
		    ((Test-Path -LiteralPath $lib) -or
		    (Test-Path -LiteralPath $staticLib))) {
			$includeFlag = "-I" + (Convert-ToGccPath $include)
			$libFlag = "-L" +
			    (Convert-ToGccPath (Join-Path $prefix "lib"))
			return [ordered]@{
				Cflags = @($includeFlag)
				Libs = @($libFlag, "-levent")
				BinPaths = @(Join-Path $prefix "bin")
			}
		}
	}
	return $null
}

function Add-PathEntries([string[]]$Paths) {
	$current = @($env:PATH -split ";" | Where-Object { $_ -ne "" })
	foreach ($path in $Paths) {
		if ([string]::IsNullOrWhiteSpace($path) -or
		    -not (Test-Path -LiteralPath $path)) {
			continue
		}
		$resolved = (Resolve-Path -LiteralPath $path).Path
		$exists = $false
		foreach ($entry in $current) {
			if ($entry -ieq $resolved) {
				$exists = $true
				break
			}
		}
		if (-not $exists) {
			$current = @($resolved) + $current
		}
	}
	$env:PATH = ($current -join ";")
}

function Get-LibeventFlags {
	$result = [ordered]@{
		Cflags = @()
		Libs = @()
		BinPaths = @()
	}
	if (-not [string]::IsNullOrWhiteSpace($LibeventCflags)) {
		$result.Cflags = @(Split-ArgumentString $LibeventCflags)
	}
	if (-not [string]::IsNullOrWhiteSpace($LibeventLibs)) {
		$result.Libs = @(Split-ArgumentString $LibeventLibs)
	}
	if ($result.Cflags.Count -ne 0 -or $result.Libs.Count -ne 0) {
		return $result
	}

	$msys2 = Find-Msys2LibeventFlags
	if ($msys2 -ne $null) {
		return $msys2
	}

	$pkgConfig = Get-Command pkg-config -ErrorAction SilentlyContinue
	if ($pkgConfig -ne $null) {
		& $pkgConfig.Source --exists libevent 2>$null
		if ($LASTEXITCODE -eq 0) {
			$cflags = & $pkgConfig.Source --cflags libevent 2>$null
			if ($LASTEXITCODE -ne 0) {
				throw "pkg-config failed to report libevent cflags"
			}
			$libs = & $pkgConfig.Source --libs libevent 2>$null
			if ($LASTEXITCODE -ne 0) {
				throw "pkg-config failed to report libevent libs"
			}
			$result.Cflags = Convert-Msys2PkgConfigFlags @(
			    Split-ArgumentString $cflags)
			$result.Libs = Convert-Msys2PkgConfigFlags @(
			    Split-ArgumentString $libs)
			if ($result.Libs.Count -eq 0) {
				throw "pkg-config returned no libevent libraries"
			}
			Add-LibeventBinPathsFromFlags $result
			return $result
		}
	}

	if ($pkgConfig -eq $null) {
		throw "UseSystemLibevent needs pkg-config, explicit -LibeventCflags/-LibeventLibs, or an MSYS2 libevent next to the compiler"
	}
	throw "pkg-config could not find libevent and no MSYS2 libevent prefix was inferred"
}

function Invoke-TerminalFormatProbe([string[]]$Common) {
	$probeSource = Join-Path $Temp "terminal_format_probe.c"
	$supportSource = Join-Path $Temp "terminal_format_support.c"
	$probeObject = Convert-ToObjectName $probeSource "terminal_"
	$supportObject = Convert-ToObjectName $supportSource "terminal_"
	$exe = Join-Path $Temp "terminal-format-probe.exe"
	$formatCommon = $Common + @("-ffunction-sections", "-fdata-sections")

	Invoke-Compile $CC ($formatCommon + @("-c", $supportSource, "-o", $supportObject))
	Invoke-Compile $CC ($formatCommon + @("-c", $probeSource, "-o", $probeObject))
	$linkResult = Invoke-NativeCapture $CC @(
		$probeObject, $supportObject, "-Wl,--gc-sections", "-o", $exe
	) "terminal-format-link"
	if ($linkResult.ExitCode -ne 0) {
		$linkOutput = $linkResult.Output
		$linkOutput | Select-Object -First 120 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "terminal format probe link failed"
	}
	$runResult = Invoke-NativeCapture $exe @() "terminal-format-run"
	$runOutput = $runResult.Output
	if ($runResult.ExitCode -ne 0) {
		$runOutput | Select-Object -First 80 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "terminal format probe failed"
	}
	Write-Host $runOutput
}

New-ProbeDirectory
try {
	Write-ProbeFiles
	Push-Location $Root

	$common = @(
		"-std=gnu99",
		"-D_WIN32",
		"-D_WIN32_WINNT=0x0A00",
		"-DNTDDI_VERSION=0x0A000006",
		"-DTMUX_SOCK_PERM=0",
		"-DHAVE_DAEMON",
		"-DHAVE_FDFORKPTY",
		"-DHAVE_FORKPTY",
		"-DHAVE_CFMAKERAW",
		"-DHAVE_CLOSEFROM",
		"-DHAVE_GETDTABLECOUNT",
		"-DHAVE_GETDTABLESIZE",
		"-DHAVE_GETPEEREID",
		"-include", (Join-Path $Temp "probe_config.h"),
		"-I.", "-Icompat", "-I$Temp", "-iquote."
	)
	$linkLibraries = @()
	$eventMode = "stub"
	$parserMode = "stub"

	if ($UseSystemLibevent) {
		$eventFlags = Get-LibeventFlags
		$common += @("-DHAVE_EVENT2_EVENT_H") + $eventFlags.Cflags
		$linkLibraries += $eventFlags.Libs
		Add-PathEntries $eventFlags.BinPaths
		$eventMode = "system"
	}
	$linkLibraries += @("-lws2_32", "-ladvapi32", "-lbcrypt", "-luserenv",
	    "-lshlwapi", "-lshell32", "-lole32", "-luuid")

	Invoke-TerminalFormatProbe $common

	$objects = New-Object System.Collections.Generic.List[string]

	if (-not $UseSystemLibevent) {
		$source = Join-Path $Temp "event_stub.c"
		$object = Convert-ToObjectName $source "probe_"
		Invoke-Compile $CC ($common + @("-c", $source, "-o", $object))
		$objects.Add($object)
	}

	if ($UseGeneratedParser) {
		$yaccTool = Find-Yacc
		if ([string]::IsNullOrWhiteSpace($yaccTool)) {
			throw "UseGeneratedParser requested, but no yacc/bison executable was found"
		}
		$generatedParser = Join-Path $Temp "cmd-parse.c"
		$yaccResult = Invoke-NativeCapture $yaccTool @(
			"-d", "-o", $generatedParser, "cmd-parse.y"
		) "yacc"
		$yaccOutput = $yaccResult.Output
		if ($yaccResult.ExitCode -ne 0) {
			$yaccOutput | Select-Object -First 120 | ForEach-Object {
				[Console]::Error.WriteLine($_)
			}
			throw "parser generation failed: $yaccTool"
		}
		$source = $generatedParser
		$parserMode = "generated"
	} else {
		$source = Join-Path $Temp "parser_stub.c"
	}
	$object = Convert-ToObjectName $source "probe_"
	Invoke-Compile $CC ($common + @("-c", $source, "-o", $object))
	$objects.Add($object)

	# cmd-parse.c is the generated parser and is compiled separately above.
	# Exclude it here so a copy left in the tree (some bison builds ignore
	# -o and emit cmd-parse.c into the working directory) is not compiled a
	# second time, which would cause duplicate-symbol link errors.
	$rootFiles = Get-ChildItem -LiteralPath $Root -Filter "*.c" -File |
	    Where-Object {
		    $_.Name -notin @("image.c", "image-sixel.c", "cmd-parse.c") -and
		    (($_.Name -notlike "osdep-*.c") -or ($_.Name -eq "osdep-windows.c"))
	    } |
	    Sort-Object Name
	foreach ($file in $rootFiles) {
		$object = Convert-ToObjectName $file.Name "root_"
		Invoke-Compile $CC ($common + @("-c", $file.FullName, "-o", $object))
		$objects.Add($object)
	}

	$win32Files = @("compat/imsg.c", "compat/imsg-buffer.c") +
	    (Get-ChildItem -LiteralPath (Join-Path $Root "compat") -Filter "win32-*.c" -File |
	    Sort-Object Name | ForEach-Object { "compat/$($_.Name)" })
	foreach ($source in $win32Files) {
		$object = Convert-ToObjectName $source "compat_"
		Invoke-Compile $CC ($common + @("-c", $source, "-o", $object))
		$objects.Add($object)
	}

	$regexObject = Join-Path $Temp "compat_win32_regex.o"
	Invoke-Compile $CXX @(
		"-std=gnu++11", "-D_WIN32", "-D_WIN32_WINNT=0x0A00",
		"-DNTDDI_VERSION=0x0A000006", "-I.", "-Icompat",
		"-c", "compat/win32-regex.cc", "-o", $regexObject
	)
	$objects.Add($regexObject)

	$generic = @(
		"compat/asprintf.c", "compat/base64.c", "compat/clock_gettime.c",
		"compat/err.c", "compat/explicit_bzero.c", "compat/fgetln.c",
		"compat/freezero.c", "compat/getline.c", "compat/getprogname.c",
		"compat/getopt_long.c", "compat/htonll.c", "compat/memmem.c",
		"compat/ntohll.c", "compat/reallocarray.c", "compat/recallocarray.c",
		"compat/setenv.c", "compat/setproctitle.c", "compat/strcasestr.c",
		"compat/strlcat.c", "compat/strlcpy.c", "compat/strndup.c",
		"compat/strnlen.c", "compat/strsep.c", "compat/strtonum.c",
		"compat/unvis.c", "compat/vis.c"
	)
	$generic = $generic | Where-Object {
		$_ -ne "compat/clock_gettime.c"
	}
	foreach ($source in $generic) {
		$object = Convert-ToObjectName $source "generic_"
		Invoke-Compile $CC ($common + @("-c", $source, "-o", $object))
		$objects.Add($object)
	}

	$exe = Join-Path $Temp "tmux-probe.exe"
	$linkResponse = Join-Path $Temp "probe-link.rsp"
	$linkArguments = @($objects.ToArray()) + $linkLibraries + @("-o", $exe)
	Write-ResponseFile $linkResponse $linkArguments
	$linkResult = Invoke-NativeCapture $CXX @("@$linkResponse") "probe-link"
	$linkOutput = $linkResult.Output
	if ($linkResult.ExitCode -ne 0) {
		[Console]::Error.WriteLine("link libraries: " +
		    ($linkLibraries -join " "))
		[Console]::Error.WriteLine("link response: $linkResponse")
		$linkOutput | Select-Object -First 160 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "probe link failed"
	}
	$runResult = Invoke-NativeCapture $exe @("-V") "probe-run"
	$runOutput = $runResult.Output
	if ($runResult.ExitCode -ne 0) {
		$runOutput | Select-Object -First 80 | ForEach-Object {
			[Console]::Error.WriteLine($_)
		}
		throw "probe run failed"
	}
	Write-Host "probe link succeeded: $exe"
	Write-Host "probe run: $runOutput"
	Write-Host "root=$($rootFiles.Count) win32_compat=$($win32Files.Count) generic=$($generic.Count)"
	Write-Host "parser=$parserMode libevent=$eventMode"
	if (-not [string]::IsNullOrWhiteSpace($OutputExe)) {
		$output = $OutputExe
		if (-not [System.IO.Path]::IsPathRooted($output)) {
			$output = Join-Path $Root $output
		}
		$outputDirectory = Split-Path -Parent $output
		if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
			New-Item -ItemType Directory -Force -Path $outputDirectory |
			    Out-Null
		}
		Copy-Item -LiteralPath $exe -Destination $output -Force
		Write-Host "output=$output"

		# Copy the mingw runtime DLLs the binary links against next to
		# the output executable, so tmux.exe can be launched from any
		# directory without D:\msys64\ucrt64\bin on PATH. Without this the
		# process fails to load and exits silently with no output.
		$ccCommand = Get-Command $CC -ErrorAction SilentlyContinue
		if ($ccCommand -and $ccCommand.Source) {
			$ccDir = Split-Path -Parent $ccCommand.Source
			$runtimeDlls = @('libevent-7.dll', 'libgcc_s_seh-1.dll',
			    'libwinpthread-1.dll', 'libstdc++-6.dll')
			foreach ($dll in $runtimeDlls) {
				$dllSource = Join-Path $ccDir $dll
				if (Test-Path -LiteralPath $dllSource) {
					Copy-Item -LiteralPath $dllSource `
					    -Destination $outputDirectory -Force
					Write-Host "runtime dll: $dll"
				}
			}
		}
	}
}
finally {
	Pop-Location
	if (-not $KeepTemp -and (Test-Path -LiteralPath $Temp)) {
		$resolved = (Resolve-Path -LiteralPath $Temp).Path
		$prefix = $Root + [System.IO.Path]::DirectorySeparatorChar
		if ($resolved.StartsWith($prefix)) {
			Remove-Item -LiteralPath $resolved -Recurse -Force
		}
	}
}
