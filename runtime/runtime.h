/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* Shared between the native title's run-time glue (runtime.c), the path shim (shim/ps5path.c with
 * -DPS5PATH_NATIVE) and the launcher. */
#ifndef OPS5_RUNTIME_H
#define OPS5_RUNTIME_H

/* Where the title keeps what it writes: the log, settings, the engines' own files. Readable over FTP. */
#define OPS5_DATA_DIR "/data/outsider-ps5"
#define OPS5_LOG OPS5_DATA_DIR "/last-run.log"
#define OPS5_LOG_PREV OPS5_DATA_DIR "/last-run.prev.log"
/* The log also goes out over UDP (native/runtime.c): to this port, broadcast, or to the IPv4 address in this file */
#define OPS5_LOG_PORT 18194
#define OPS5_LOG_HOST_FILE OPS5_DATA_DIR "/log-host"
/* Names a checkpoint at which the title crashes on purpose (native/runtime.c, ops5_checkpoint()) */
#define OPS5_TRAP_FILE OPS5_DATA_DIR "/trap-at"

/* When this file exists the title runs native/runtime.c's JIT probe at start-up: it tries to get executable memory
 * the way a JIT would and logs what happened ("title: jit-probe" lines). */
#define OPS5_JIT_PROBE_FILE OPS5_DATA_DIR "/jit-probe"

/* The title's own (read-only) folder: the launcher's font, Outsider's shims, rgss_compat.rb */
#define OPS5_APP_DIR "/app0"

#define OPS5_PATH_MAX 1024

#ifdef __cplusplus
extern "C" {
#endif

/* Records the working directory after a successful chdir() (getcwd() does not work in a title). */
void ops5_set_cwd(const char *abs);

/* Writes one line to the log with write(), bypassing stdio (for start-up and shutdown diagnostics). */
void ops5_log(const char *line);
/* The log file only (no kernel log, no UDP); buffered when the flusher runs. For long reports. */
void ops5_log_buffered(const char *line);

/* From here on lines are buffered and written by a thread (title:/error lines still at once), and stdout/stderr are
 * fully buffered and flushed by the same thread. A write to the log file costs 50-150 ms on the console. */
void ops5_log_start_flusher(void);
/* Writes out everything buffered and syncs the file. */
void ops5_log_flush(void);

/* Crashes the title on purpose (SIGILL) when OPS5_TRAP_FILE names this stage: proof in the kernel log that the
 * title got this far, when no log does. */
void ops5_checkpoint(const char *stage);

#ifdef __cplusplus
}
#endif

#endif
