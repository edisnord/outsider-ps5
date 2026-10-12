/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* Run-time glue for Outsider PS5 as a native PS5 title (GPU OpenGL through ps5-opengl), linked into the
 * title by native/title-link.sh. Taken from the mkxp PS5 port (ps5/native/mkxp_native.c), where these were
 * found on a console:
 *
 *  - stdout and stderr go to /data/outsider-ps5/last-run.log (a title has no console);
 *  - the size of ps5-opengl's heap;
 *  - replacements for the libc functions that the payload SDK's libc.a implements as raw "syscall"
 *    instructions, which a title may not execute (the console kills it): mmap, mprotect, ppoll, umask,
 *    chown, lchown, lchmod, getcwd. title-link.sh routes them here with --wrap. (readlink() and chdir(),
 *    also on that list, are in shim/ps5path.c, which wraps them anyway.)
 *  - exit(), _exit() and _Exit() ask the shell to close the title (LoadExec("exit")), as a title must.
 *
 * shim/ps5path.c (built with -DPS5PATH_NATIVE) wraps the path functions on top of this: relative paths,
 * case-insensitive names, chdir() (it reports the new directory with ops5_set_cwd()), and opendir()
 * on folders the title sandbox refuses to open as directories.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <setjmp.h>
#include <stdarg.h>
#include <signal.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "runtime.h"

/* --- log ------------------------------------------------------------------------------------------- */

/* The previous run's log is kept as last-run.prev.log, where the launcher looks for the last error.
 * stdout and stderr share one descriptor: opened twice, they would have separate offsets and overwrite
 * each other's lines. */
/* The log's own descriptor: ops5_log() writes to it with write(), so that a line gets through even when
 * stdio does not (before or after the dup2() calls take, or with the heap in trouble) */
static int log_fd = -1;

/* The same lines also go out as UDP datagrams, to port OPS5_LOG_PORT: broadcast on the local network, or to
 * the address in OPS5_LOG_HOST_FILE when it exists. Listen on a PC with `socat -u UDP-RECV:18194 -` (or
 * `nc -lku 18194`). A file written by the title is not always readable over FTP while it runs (the mkxp port
 * found that too), and this needs no file at all. */
static int udp_fd = -1;
static struct sockaddr_in udp_to;

static void udp_open(void)
{
	char host[64] = "";
	int one = 1;

	int fd = open(OPS5_LOG_HOST_FILE, O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, host, sizeof host - 1);
		close(fd);
		host[n > 0 ? n : 0] = '\0';
		host[strcspn(host, " \r\n")] = '\0';
	}
	udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_fd < 0)
		return;
	memset(&udp_to, 0, sizeof udp_to);
	udp_to.sin_family = AF_INET;
	udp_to.sin_port = htons(OPS5_LOG_PORT);
	if (host[0] && inet_pton(AF_INET, host, &udp_to.sin_addr) == 1)
		return;
	udp_to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	setsockopt(udp_fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
}

static void ops5_open_log(void);
static int log_opened;

/* Checkpoints for when no log gets out: the file OPS5_TRAP_FILE names one (ctor, main, sdl or exit), and
 * the title crashes on purpose (SIGILL) when it reaches it. The kernel log (klogsrv) then shows
 * "exited on signal 4" for the title instead of its usual LoadExec("exit"), with a crash report. */
void ops5_checkpoint(const char *stage)
{
	static char want[32];
	static int loaded;
	if (!loaded) {
		loaded = 1;
		int fd = open(OPS5_TRAP_FILE, O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, want, sizeof want - 1);
			close(fd);
			want[n > 0 ? n : 0] = '\0';
			want[strcspn(want, " \r\n")] = '\0';
		}
	}
	if (want[0] && strcmp(want, stage) == 0)
		__builtin_trap();
#ifdef OPS5_TRAP_AT
	/* Chosen when the title was linked (OPS5_TRAP_AT=<stage> for native/title-link.sh): needs no file */
	if (strcmp(OPS5_TRAP_AT, stage) == 0)
		__builtin_trap();
#endif
}

/* libkernel: writes to the kernel log (/dev/klog), which klogsrv serves on port 3232. Needs neither a file nor
 * the network, so it is the first place a line goes. */
int sceKernelDebugOutText(int channel, const char *text);

static long long log_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* A write() to the log file took 50-150 ms on the console and got slower as the file grew, and in a long run one
 * stalled the game for 36 s. Everything below is arranged so that the game thread never waits for I/O once
 * ops5_log_start_flusher() has run:
 *
 *  - lines (and what the game prints to stdout/stderr, see below) are copied into one of two buffers under
 *    log_mutex, which is only ever held for a memcpy;
 *  - one thread swaps the buffers, writes the full one to the file with the mutex released, sends the kernel-log and
 *    UDP copies of ops5_log() lines, and fsyncs every few seconds;
 *  - when the buffer being filled is full while the thread is still writing the other one, lines are dropped and
 *    counted, never waited for;
 *  - stdout and stderr are streams over a write function that appends to the same buffer. They used to be files
 *    with big stdio buffers, whose flush held the stream's lock for the whole (slow) write, and the game thread's
 *    next printf waited for that lock.
 * Until the flusher starts, and from ops5_log_flush() on the way out, lines are written at once. */
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_kick = PTHREAD_COND_INITIALIZER;
#define LOG_BUF_SIZE (192 * 1024)
#define NET_BUF_SIZE (64 * 1024)
static char log_bufs[2][LOG_BUF_SIZE];
static size_t log_lens[2];
static int log_cur;
static unsigned long log_dropped;
static char net_buf[NET_BUF_SIZE];   /* ops5_log() lines for the kernel log and UDP, each NUL-terminated */
static size_t net_len;
static unsigned long net_dropped;
static int log_buffering; /* set by ops5_log_start_flusher() */

/* log_mutex held */
static void log_append_locked(const char *p, size_t n)
{
	if (n == 0)
		return;
	if (log_lens[log_cur] + n > LOG_BUF_SIZE) {
		log_dropped += n;
		return;
	}
	memcpy(log_bufs[log_cur] + log_lens[log_cur], p, n);
	log_lens[log_cur] += n;
}

static void log_write_all(const char *p, size_t n)
{
	size_t off = 0;
	while (off < n) {
		ssize_t w = write(log_fd, p + off, n - off);
		if (w <= 0)
			break;
		off += (size_t)w;
	}
}

static int log_is_urgent(const char *line)
{
	return strncmp(line, "title:", 6) == 0 || strncmp(line, "outsider-vk:", 12) == 0 ||
	       strstr(line, "[ERROR]") != NULL || strstr(line, "Exception") != NULL || strstr(line, "[stall]") != NULL;
}

void ops5_log(const char *line)
{
	/* exit() can come before the constructor below has run (from start-up code or another library's
	 * constructor): open the log on first use */
	if (!log_buffering)
		sceKernelDebugOutText(0, line);
	if (!log_opened)
		ops5_open_log();
	size_t len = strlen(line);
	if (!log_buffering) {
		if (udp_fd >= 0)
			sendto(udp_fd, line, len, 0, (const struct sockaddr *)&udp_to, sizeof udp_to);
		if (log_fd < 0)
			return;
		pthread_mutex_lock(&log_mutex);
		write(log_fd, line, len);
		fsync(log_fd);
		pthread_mutex_unlock(&log_mutex);
		return;
	}
	pthread_mutex_lock(&log_mutex);
	if (log_fd >= 0)
		log_append_locked(line, len);
	if (net_len + len + 1 <= NET_BUF_SIZE) {
		memcpy(net_buf + net_len, line, len + 1);
		net_len += len + 1;
	} else {
		net_dropped++;
	}
	if (log_is_urgent(line))
		pthread_cond_signal(&log_kick);
	pthread_mutex_unlock(&log_mutex);
}

/* A line for the log file only: no kernel log, no UDP (both cost 50-150 ms a line here and grow). For long
 * reports the game must not wait for. */
void ops5_log_buffered(const char *line)
{
	if (!log_opened)
		ops5_open_log();
	if (log_fd < 0)
		return;
	size_t len = strlen(line);
	pthread_mutex_lock(&log_mutex);
	if (log_buffering) {
		log_append_locked(line, len);
	} else {
		write(log_fd, line, len);
	}
	pthread_mutex_unlock(&log_mutex);
}

/* stdout and stderr: the title is linked with --wrap for the functions below (vulkan/title-link.sh), so a
 * message the game or a library prints to either stream is appended to the buffer here and never touches a FILE
 * (whose lock a flush of the file would hold for a whole slow write, with the game thread's next printf waiting
 * for it: a 36 s freeze in a console run, in a call that had nothing to do with the log). Other streams, and
 * everything before the flusher runs, go to the real functions. */
int __real_vfprintf(FILE *f, const char *fmt, va_list ap);
int __real_fputs(const char *s, FILE *f);
size_t __real_fwrite(const void *p, size_t size, size_t n, FILE *f);
int __real_fputc(int c, FILE *f);

static int console_stream(FILE *f)
{
	return log_buffering && (f == stdout || f == stderr);
}

static void console_write(const char *p, size_t n)
{
	pthread_mutex_lock(&log_mutex);
	log_append_locked(p, n);
	pthread_mutex_unlock(&log_mutex);
}

int __wrap_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	if (!console_stream(f))
		return __real_vfprintf(f, fmt, ap);
	char small[1024];
	va_list again;
	va_copy(again, ap);
	int n = vsnprintf(small, sizeof small, fmt, ap);
	if (n >= 0) {
		if ((size_t)n < sizeof small) {
			console_write(small, (size_t)n);
		} else {
			char *big = malloc((size_t)n + 1);
			if (big) {
				vsnprintf(big, (size_t)n + 1, fmt, again);
				console_write(big, (size_t)n);
				free(big);
			}
		}
	}
	va_end(again);
	return n;
}

int __wrap_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = __wrap_vfprintf(f, fmt, ap);
	va_end(ap);
	return n;
}

int __wrap_vprintf(const char *fmt, va_list ap)
{
	return __wrap_vfprintf(stdout, fmt, ap);
}

int __wrap_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = __wrap_vfprintf(stdout, fmt, ap);
	va_end(ap);
	return n;
}

int __wrap_fputs(const char *s, FILE *f)
{
	if (!console_stream(f))
		return __real_fputs(s, f);
	console_write(s, strlen(s));
	return 1;
}

int __wrap_puts(const char *s)
{
	if (!log_buffering)
		return __real_fputs(s, stdout) < 0 ? -1 : __real_fputc('\n', stdout);
	console_write(s, strlen(s));
	console_write("\n", 1);
	return 1;
}

size_t __wrap_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	if (!console_stream(f))
		return __real_fwrite(p, size, n, f);
	console_write(p, size * n);
	return n;
}

int __wrap_fputc(int c, FILE *f)
{
	if (!console_stream(f))
		return __real_fputc(c, f);
	char ch = (char)c;
	console_write(&ch, 1);
	return (unsigned char)c;
}

int __wrap_putc(int c, FILE *f)
{
	return __wrap_fputc(c, f);
}

int __wrap_putchar(int c)
{
	return __wrap_fputc(c, stdout);
}

/* Writes out everything that is buffered, and what stdio holds. Used on the way out, where waiting is fine. */
void ops5_log_flush(void)
{
	fflush(NULL);
	pthread_mutex_lock(&log_mutex);
	if (log_fd >= 0) {
		/* what is being filled; the thread finishes the buffer it is writing itself */
		if (log_lens[log_cur]) {
			log_write_all(log_bufs[log_cur], log_lens[log_cur]);
			log_lens[log_cur] = 0;
		}
		fsync(log_fd);
	}
	for (size_t off = 0; off < net_len; off += strlen(net_buf + off) + 1)
		sceKernelDebugOutText(0, net_buf + off);
	net_len = 0;
	pthread_mutex_unlock(&log_mutex);
}

static void *log_flusher(void *arg)
{
	(void)arg;
	static char net_out[NET_BUF_SIZE];
	static int reported;
	long long last_sync_us = log_now_us();
	for (;;) {
		struct timespec dl;
		clock_gettime(CLOCK_REALTIME, &dl);
		dl.tv_nsec += 500 * 1000000L;
		if (dl.tv_nsec >= 1000000000L) {
			dl.tv_sec++;
			dl.tv_nsec -= 1000000000L;
		}
		pthread_mutex_lock(&log_mutex);
		/* woken at once by an urgent line, else every half second */
		if (log_lens[log_cur] == 0 && net_len == 0)
			pthread_cond_timedwait(&log_kick, &log_mutex, &dl);
		pthread_mutex_unlock(&log_mutex);
		fflush(stdout);   /* a partial line: a write function call, no file I/O */
		fflush(stderr);

		pthread_mutex_lock(&log_mutex);
		int w = log_cur;
		size_t wl = log_lens[w];
		if (wl)
			log_cur ^= 1;
		size_t nl = net_len;
		if (nl)
			memcpy(net_out, net_buf, nl);
		net_len = 0;
		unsigned long dropped = log_dropped, ndropped = net_dropped;
		log_dropped = 0;
		net_dropped = 0;
		pthread_mutex_unlock(&log_mutex);

		long long took = 0;
		if (wl && log_fd >= 0) {
			long long t0 = log_now_us();
			log_write_all(log_bufs[w], wl);
			took = log_now_us() - t0;
		}
		if (wl) {
			pthread_mutex_lock(&log_mutex);
			log_lens[w] = 0;
			pthread_mutex_unlock(&log_mutex);
		}
		for (size_t off = 0; off < nl; off += strlen(net_out + off) + 1) {
			const char *line = net_out + off;
			sceKernelDebugOutText(0, line);
			if (udp_fd >= 0)
				sendto(udp_fd, line, strlen(line), 0, (const struct sockaddr *)&udp_to, sizeof udp_to);
		}
		if (log_fd >= 0 && log_now_us() - last_sync_us >= 5 * 1000000LL) {
			fsync(log_fd);
			last_sync_us = log_now_us();
		}
		/* Says once in a while what a write costs here, so the log shows whether this was the right fix */
		if ((took > 200000 || dropped || ndropped) && reported < 8) {
			char note[200];
			reported++;
			snprintf(note, sizeof note, "title: log flusher wrote %zu bytes in %lld ms (dropped: %lu bytes of log, "
			         "%lu kernel-log lines)\n", wl, took / 1000, dropped, ndropped);
			ops5_log(note);
		}
	}
	return NULL;
}

/* After this the log is written by a thread. Called by a title that logs a lot (Outsider on Vulkan). */
void ops5_log_start_flusher(void)
{
	static int started;
	if (started || log_fd < 0)
		return;
	started = 1;
	fflush(stdout);
	fflush(stderr);
	/* What still reaches these streams (code the link does not wrap) goes to the file through its own buffer */
	static char out_buf[32 * 1024], err_buf[16 * 1024];
	int out_ok = freopen(OPS5_LOG, "a", stdout) != NULL;
	int err_ok = freopen(OPS5_LOG, "a", stderr) != NULL;
	if (out_ok)
		setvbuf(stdout, out_buf, _IOFBF, sizeof out_buf);
	if (err_ok)
		setvbuf(stderr, err_buf, _IOFBF, sizeof err_buf);
	/* Self-test, so that the log shows whether it worked: a 200-character write should take microseconds */
	{
		char probe[201];
		memset(probe, 'x', 200);
		probe[200] = '\0';
		log_buffering = 1;   /* so that the probe goes through the wrapper, as everything else will */
		long long a = log_now_us();
		fprintf(stdout, "title: stdio self-test %s\n", probe);
		long long b = log_now_us();
		fflush(stdout);
		long long c = log_now_us();
		log_buffering = 0;
		char note[160];
		snprintf(note, sizeof note,
		         "title: stdio self-test: 200 characters in %lld us, flushed in %lld us (reopened: out %d, err %d)\n",
		         b - a, c - b, out_ok, err_ok);
		ops5_log(note);
	}
	pthread_t t;
	if (pthread_create(&t, NULL, log_flusher, NULL) == 0) {
		pthread_detach(t);
		log_buffering = 1;
		ops5_log("title: log buffered, written by a thread; the game never waits for the file\n");
	}
}

static void ops5_jit_probe(void);

/* Before every other constructor (priority 101, the first one available to programs), or earlier, from the
 * first ops5_log() */
__attribute__((constructor(101))) static void ops5_open_log(void)
{
	if (log_opened)
		return;
	log_opened = 1;
	ops5_checkpoint("ctor");
	udp_open();
	ops5_log("title: started\n");

	/* FTP, used to copy games and read the log, runs as another user than the title */
	mkdir(OPS5_DATA_DIR, 0777);
	chmod(OPS5_DATA_DIR, 0777);
	rename(OPS5_LOG, OPS5_LOG_PREV);

	/* O_APPEND: stdio's streams below write to the same file through descriptors of their own */
	int fd = open(OPS5_LOG, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
	if (fd < 0) {
		ops5_log("title: cannot open " OPS5_LOG "\n");
		return;
	}
	log_fd = fd;
	ops5_log("title: started, log open\n");
	dup2(fd, 1);
	dup2(fd, 2);
	/* stdio does not always write to descriptors 1 and 2 (in outsider-vk.bin the engines' fprintf(stderr)
	 * never reached the log): point the streams themselves at the log as well, as ps5-homebrew-ui's runtime
	 * does */
	if (!freopen(OPS5_LOG, "a", stdout))
		ops5_log("title: cannot send stdout to the log\n");
	if (!freopen(OPS5_LOG, "a", stderr))
		ops5_log("title: cannot send stderr to the log\n");
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	ops5_jit_probe();
}

/* --- ps5-opengl ------------------------------------------------------------------------------------ */

/* app_heap.c (ps5-opengl's allocator for native titles) reserves this much and falls back to smaller
 * sizes if the mapping fails. Its default of 128 MiB is far too small for Ruby or a large MV/MZ game. */
const size_t ps5_opengl_heap_size = 1024u * 1024u * 1024u;

/* The OpenGL runtime references this TLS init function; Mesa's glapi context needs no dynamic
 * initialization. */
void ps5_opengl_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void ps5_opengl_glapi_tls_context_init(void)
{
}

/* --- libc functions that would be raw syscalls ------------------------------------------------------ */

int sceKernelMmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset, void **res);
int sceKernelMprotect(const void *addr, size_t len, int prot);
int sceKernelMunmap(void *addr, size_t len);

/* SCE errors are 0x8002xxxx with the errno in the low bits */
static int sce_error(int ret)
{
	errno = ret & 0xffff;
	return -1;
}

/* The first few failed mappings go to the log: a JIT that asks for a big executable pool and is refused turns
 * itself off without a word (JavaScriptCore does), and errno alone does not say which step refused. */
static void mmap_fail_log(const char *step, size_t len, int prot, int flags, int ret)
{
	static int count;
	char line[160];

	if (count++ >= 16)
		return;
	snprintf(line, sizeof line, "title: %s failed: len 0x%zx prot 0x%x flags 0x%x, sce 0x%x\n", step, len, prot,
	         flags, (unsigned)ret);
	ops5_log(line);
}

/* The last anonymous mapping that was asked for with PROT_EXEC (a JIT's code pool) */
static char *exec_pool;
static size_t exec_pool_len;

void *__wrap_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	void *res = MAP_FAILED;

	/* JavaScriptCore turns the first and last page of its pool into guard pages with mmap(MAP_FIXED,
	 * PROT_NONE) over them. In a title that leaves the rest of the pool unexecutable (a fetch faults with a
	 * protection violation), so inside the pool the same is done with mprotect(). */
	if ((flags & MAP_FIXED) && prot == PROT_NONE && (flags & MAP_ANON) && exec_pool && (char *)addr >= exec_pool &&
	    (char *)addr + len <= exec_pool + exec_pool_len && sceKernelMprotect(addr, len, PROT_NONE) >= 0)
		return addr;
	/* An anonymous mapping that is writable and executable at once can be mapped, but running code in it
	 * faults in a title (see the JIT probe below). Map it without PROT_EXEC and add that with mprotect(),
	 * which does work: this is the path of a JIT's code heap (JavaScriptCore). */
	int want = prot;
	int ret;

	if ((prot & PROT_EXEC) && (flags & MAP_ANON) && fd < 0)
		prot &= ~PROT_EXEC;
	ret = sceKernelMmap(addr, len, prot, flags, fd, offset, &res);
	if (ret < 0) {
		mmap_fail_log("mmap", len, prot, flags, ret);
		sce_error(ret);
		return MAP_FAILED;
	}
	if (want != prot) {
		ret = sceKernelMprotect(res, len, want);
		if (ret < 0) {
			mmap_fail_log("mmap's mprotect", len, want, flags, ret);
			sceKernelMunmap(res, len);
			sce_error(ret);
			return MAP_FAILED;
		}
		if (want & PROT_EXEC) {
			exec_pool = res;
			exec_pool_len = len;
			if (len >= (1UL << 20)) {
				char line[120];

				snprintf(line, sizeof line, "title: executable pool %p, 0x%zx bytes\n", res, len);
				ops5_log(line);
			}
		}
	}
	return res;
}

int __wrap_mprotect(void *addr, size_t len, int prot)
{
	int ret = sceKernelMprotect(addr, len, prot);

	return ret < 0 ? sce_error(ret) : 0;
}

/* --- JIT probe -------------------------------------------------------------------------------------- */

/* Can this title run code it wrote itself? Asked through __wrap_mmap and __wrap_mprotect, the path a JIT
 * (JavaScriptCore's executable allocator, say) would take. Runs once at start-up when OPS5_JIT_PROBE_FILE
 * exists; each route writes `mov eax, 0x1234; ret`, calls it, and logs the result. A fault is caught. */
static sigjmp_buf jit_jb;
static volatile int jit_sig;

static void jit_on_signal(int sig)
{
	jit_sig = sig;
	siglongjmp(jit_jb, 1);
}

static void jit_say(const char *route, const char *fmt, ...)
{
	char line[200];
	int n = snprintf(line, sizeof line, "title: jit-probe %s: ", route);
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
	va_end(ap);
	ops5_log(line);
}

/* Calls the code at p; returns whether it gave 0x1234 */
static int jit_call(void *p, const char *route)
{
	int r = 0;

	jit_sig = 0;
	if (sigsetjmp(jit_jb, 1) == 0) {
		r = ((int (*)(void))p)();
	} else {
		jit_say(route, "call FAULTED, signal %d\n", (int)jit_sig);
		return 0;
	}
	jit_say(route, "call returned 0x%x\n", r);
	return r == 0x1234;
}

static void jit_route(const char *name, int prot0, int prot1)
{
	static const unsigned char code[] = { 0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3 };
	void *p = __wrap_mmap(NULL, 0x4000, prot0, MAP_PRIVATE | MAP_ANON, -1, 0);

	if (p == MAP_FAILED) {
		jit_say(name, "mmap failed, errno %d\n", errno);
		return;
	}
	if (prot0 & PROT_WRITE)
		memcpy(p, code, sizeof code);
	if (prot1 >= 0 && __wrap_mprotect(p, 0x4000, prot1) != 0) {
		jit_say(name, "mprotect failed, errno %d\n", errno);
		return;
	}
	jit_say(name, "%s\n", jit_call(p, name) ? "OK, ran" : "NO");
}

/* JavaScriptCore reserves its code pool (512 MB on x86-64) as one anonymous RWX mapping */
static void jit_size_route(size_t size)
{
	static const unsigned char code[] = { 0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3 };
	char name[48];
	void *p;

	snprintf(name, sizeof name, "pool %zu MB RWX", size >> 20);
	p = __wrap_mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) {
		jit_say(name, "mmap failed, errno %d\n", errno);
		return;
	}
	memcpy(p, code, sizeof code);
	jit_say(name, "%s\n", jit_call(p, name) ? "OK, ran" : "NO");
	sceKernelMunmap(p, size);
}

/* JavaScriptCore's reservation: RWX mapping with a guard page remapped (MAP_FIXED, PROT_NONE) at each end,
 * the code written to the first usable page. Done with the raw SCE calls, bypassing the pool handling in
 * __wrap_mmap, to see whether the guard remap is what makes the rest unexecutable. */
static void jit_guard_route(int use_mprotect)
{
	static const unsigned char code[] = { 0xb8, 0x34, 0x12, 0x00, 0x00, 0xc3 };
	const size_t guard = 0x4000, size = 64UL << 20, total = size + 2 * guard;
	const char *name = use_mprotect ? "pool with guards by mprotect" : "pool with guards by mmap";
	void *p = MAP_FAILED, *q = MAP_FAILED;
	int ret = sceKernelMmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0, &p);

	if (ret < 0 || sceKernelMprotect(p, total, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
		jit_say(name, "mapping failed, sce 0x%x\n", (unsigned)ret);
		return;
	}
	if (use_mprotect) {
		sceKernelMprotect(p, guard, PROT_NONE);
		sceKernelMprotect((char *)p + total - guard, guard, PROT_NONE);
	} else {
		sceKernelMmap(p, guard, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0, &q);
		sceKernelMmap((char *)p + total - guard, guard, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0, &q);
	}
	memcpy((char *)p + guard + 0x4980, code, sizeof code);
	jit_say(name, "%s\n", jit_call((char *)p + guard + 0x4980, name) ? "OK, ran" : "NO");
	sceKernelMunmap(p, total);
}

static void ops5_jit_probe(void)
{
	/* JavaScriptCore prints its final options at start (is useJIT on?) when this is set */
	if (access(OPS5_DATA_DIR "/jsc-dump-options", F_OK) == 0)
		setenv("RMMZ_JSC_DUMP_OPTIONS", "1", 1);
	if (access(OPS5_JIT_PROBE_FILE, F_OK) != 0)
		return;
	ops5_log("title: jit-probe start\n");
	signal(SIGSEGV, jit_on_signal);
	signal(SIGBUS, jit_on_signal);
	signal(SIGILL, jit_on_signal);
	jit_route("mmap RWX", PROT_READ | PROT_WRITE | PROT_EXEC, -1);
	jit_route("mmap RW then mprotect RX", PROT_READ | PROT_WRITE, PROT_READ | PROT_EXEC);
	jit_route("mmap RW then mprotect RWX", PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE | PROT_EXEC);
	jit_size_route(1UL << 20);
	jit_size_route(16UL << 20);
	jit_size_route(128UL << 20);
	jit_size_route(512UL << 20);
	jit_size_route(1UL << 30);
	jit_guard_route(1);
	jit_guard_route(0);
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	signal(SIGILL, SIG_DFL);
	ops5_log("title: jit-probe done\n");
}

int __wrap_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *ts, const void *sigmask)
{
	(void)sigmask;
	int timeout = -1;

	if (ts) {
		long long ms = (long long)ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000;
		timeout = ms > 0x7fffffff ? 0x7fffffff : (int)ms;
	}
	return poll(fds, nfds, timeout);
}

static mode_t current_umask = 022;

mode_t __wrap_umask(mode_t mask)
{
	mode_t old = current_umask;

	current_umask = mask & 0777;
	return old;
}

int __wrap_chown(const char *path, uid_t uid, gid_t gid)
{
	(void)path;
	(void)uid;
	(void)gid;
	errno = ENOSYS;
	return -1;
}

int __wrap_lchown(const char *path, uid_t uid, gid_t gid)
{
	return __wrap_chown(path, uid, gid);
}

int __wrap_lchmod(const char *path, mode_t mode)
{
	(void)path;
	(void)mode;
	errno = ENOSYS;
	return -1;
}

/* --- the working directory ------------------------------------------------------------------------- */

/* getcwd() does not work in a title (the call jumps to a garbage address), so the working directory is
 * tracked here. A title starts in /app0, its own folder. */
static char cwd[OPS5_PATH_MAX] = "/app0";

void ops5_set_cwd(const char *abs)
{
	char out[OPS5_PATH_MAX];
	char full[OPS5_PATH_MAX];
	size_t len = 0;
	char *part, *save;

	/* Resolve "." and ".." so that the path stays absolute and plain */
	snprintf(full, sizeof full, "%s", abs);
	out[0] = '\0';
	for (part = strtok_r(full, "/", &save); part; part = strtok_r(NULL, "/", &save)) {
		if (!strcmp(part, "."))
			continue;
		if (!strcmp(part, "..")) {
			char *slash = strrchr(out, '/');
			if (slash) {
				*slash = '\0';
				len = (size_t)(slash - out);
			}
			continue;
		}
		size_t plen = strlen(part);
		if (len + 1 + plen >= sizeof out)
			continue;
		out[len++] = '/';
		memcpy(out + len, part, plen + 1);
		len += plen;
	}
	snprintf(cwd, sizeof cwd, "%s", len ? out : "/");
}

char *__wrap_getcwd(char *buf, size_t size)
{
	size_t need = strlen(cwd) + 1;

	if (!buf) {
		buf = malloc(size < need ? need : size);
		if (!buf)
			return NULL;
	} else if (size < need) {
		errno = ERANGE;
		return NULL;
	}
	memcpy(buf, cwd, need);
	return buf;
}

/* --- ending the title ------------------------------------------------------------------------------ */

/* A title must not end through the kernel's exit system call, which faults: it asks the shell to close
 * it instead, as PS5 vkQuake does (PS5_vkQuake src/title_exit.cpp). The boilerplate's start-up code calls
 * exit() when main() returns, and the engines call exit() or _exit() themselves; title-link.sh routes all
 * of them here. The shell closes the title asynchronously, so this does not return. atexit() handlers do
 * not run: the process ends with the title, and stdio is flushed here. */
int sceSystemServiceLoadExec(const char *path, const char *const *argv);

/* `caller` is where exit() was called from. With title_exit's own address it locates the caller in
 * eboot.elf.unstripped: caller - title_exit + (title_exit's address in `llvm-nm eboot.elf.unstripped`),
 * then `llvm-symbolizer --obj=eboot.elf.unstripped <that address>`. */
__attribute__((noreturn, noinline)) static void title_exit(int status, const char *how, void *caller)
{
	ops5_checkpoint("exit");
	char line[256];
	snprintf(line, sizeof line, "title: %s(%d) called from %p (title_exit is at %p)\n", how, status, caller,
	         (void *)&title_exit);
	ops5_log(line);
	fprintf(stderr, "title: exit status %d, asking the shell to close the title\n", status);
	fflush(NULL);
	ops5_log_flush();   /* the log may be buffered by a thread: get the last lines out */
	int result = sceSystemServiceLoadExec("exit", NULL);
	fprintf(stderr, "title: shell request returned 0x%08x\n", (unsigned)result);
	fflush(NULL);
	for (;;)
		usleep(100000);
}

__attribute__((noreturn)) void __wrap_exit(int status)
{
	title_exit(status, "exit", __builtin_return_address(0));
}

__attribute__((noreturn)) void __wrap__exit(int status)
{
	title_exit(status, "_exit", __builtin_return_address(0));
}

__attribute__((noreturn)) void __wrap__Exit(int status)
{
	title_exit(status, "_Exit", __builtin_return_address(0));
}
