/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* The title's program: Outsider (RPG Maker MV and MZ) on SDL3's GPU API, drawing with RADV (Mesa's Vulkan driver,
 * linked in by scripts/link-title.sh). runtime.c (log, working directory, exit through the shell) and ps5path.c
 * (paths) are linked in. */
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

#include "runtime.h"

/* A game to start, in the order tried: the launch file a front end writes (three lines: the working directory, the
 * environment and the arguments, the last two with spaces escaped by a backslash), or GAME_FILE, one line naming a
 * game folder, or the first folder with an index.html (or www/index.html) under the game roots. */
#define OPS5_LAUNCH OPS5_DATA_DIR "/launch.txt"
#define OPS5_GAME_FILE OPS5_DATA_DIR "/game"
#define OPS5_SHIMS OPS5_APP_DIR "/outsider/shims"

int outsider_main(int argc, char **argv);

/* Outsider's log (src/engine/error_handler.c), wrapped by vulkan/title-link.sh: every message also goes to the
 * title log, the kernel log and UDP (native/runtime.c), so a game that stops early says why without FTP.
 * Calls inside error_handler.c itself are not wrapped. */
void __real_error_handler_log(int level, const char *fmt, ...);
const char *error_handler_level_name(int level);

void __wrap_error_handler_log(int level, const char *fmt, ...)
{
	char msg[1024], line[1100];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof msg, fmt, args);
	va_end(args);
	snprintf(line, sizeof line, "outsider: [%s] %s\n", error_handler_level_name(level), msg);
	/* The script profiler's report is about 100 lines every 10 s. Through the kernel log and UDP it took 6.6 s and
	 * then 20 s on the console, and the game stood still for that long. Those lines go to the title log only,
	 * through the buffer the flusher thread writes (a memcpy), and not through Outsider's own handler: that one
	 * writes from a thread that fell minutes behind a profiled run and lost its tail when the title was closed. */
	if (strncmp(msg, "[profile]", 9) == 0) {
		snprintf(line, sizeof line, "outsider: [%s] %s\n", error_handler_level_name(level), msg);
		ops5_log_buffered(line);
		return;
	}
	ops5_log(line);
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	__real_error_handler_log(level, "%s", msg);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	long long us = (long long)(t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
	static int reported;
	if (us > 100000 && reported < 25) {
		reported++;
		snprintf(line, sizeof line, "title: slow Outsider log handler, %lld ms (%.48s)\n", us / 1000, msg);
		ops5_log(line);
	}
}

/* The format the launcher writes (escape_arg() in launcher/main.c): words split at spaces that are not
 * escaped with a backslash. In place; returns the number of words. */
static int split_escaped(char *s, char **out, int max)
{
	int n = 0;
	char *w = s;
	while (*s && n < max) {
		while (*s == ' ')
			s++;
		if (!*s)
			break;
		out[n++] = w;
		while (*s && *s != ' ') {
			if (*s == '\\' && s[1])
				s++;
			*w++ = *s++;
		}
		if (*s)
			s++;
		*w++ = '\0';
	}
	return n;
}

/* One line of the hand-off file, without its line break */
static int read_line(FILE *f, char *buf, size_t size)
{
	if (!fgets(buf, (int)size, f))
		return -1;
	buf[strcspn(buf, "\r\n")] = '\0';
	return 0;
}

static int is_game_dir(const char *dir)
{
	char p[OPS5_PATH_MAX];
	struct stat st;
	snprintf(p, sizeof p, "%s/index.html", dir);
	if (stat(p, &st) == 0)
		return 1;
	snprintf(p, sizeof p, "%s/www/index.html", dir);
	return stat(p, &st) == 0;
}

/* The first game folder under a root, as its path (the folder with the index.html, www/ included) */
static int first_game(const char *root, char *out, size_t size)
{
	DIR *d = opendir(root);
	if (!d)
		return 0;
	struct dirent *e;
	int found = 0;
	while (!found && (e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		char p[OPS5_PATH_MAX];
		snprintf(p, sizeof p, "%s/%s", root, e->d_name);
		if (is_game_dir(p)) {
			snprintf(out, size, "%s", p);
			found = 1;
		}
	}
	closedir(d);
	return found;
}

/* The game root Outsider is given: the folder holding index.html */
static void game_root(char *path)
{
	char p[OPS5_PATH_MAX];
	struct stat st;
	snprintf(p, sizeof p, "%s/www/index.html", path);
	if (stat(p, &st) == 0) {
		size_t n = strlen(path);
		snprintf(path + n, OPS5_PATH_MAX - n, "/www");
	}
}

static void escape_into(char *out, size_t size, const char *in)
{
	size_t n = 0;
	for (; *in && n + 2 < size; in++) {
		if (*in == ' ' || *in == '\\')
			out[n++] = '\\';
		out[n++] = *in;
	}
	out[n] = '\0';
}

int main(void)
{
	static char home[OPS5_PATH_MAX], env[2600], args[1400];
	static char line[1600]; /* to the title log, the kernel log and UDP (runtime.c) */

	FILE *f = fopen(OPS5_LAUNCH, "r");
	int ok = 0;
	const int diag = access(OPS5_DATA_DIR "/diag", F_OK) == 0;
	if (f) {
		ok = read_line(f, home, sizeof home) == 0 && read_line(f, env, sizeof env) == 0 &&
		     read_line(f, args, sizeof args) == 0;
		fclose(f);
		/* Once only: a crash must not bring the same game back at the next start */
		unlink(OPS5_LAUNCH);
	}
	if (!ok) {
		static char game[OPS5_PATH_MAX], esc[2 * OPS5_PATH_MAX];
		game[0] = '\0';
		FILE *g = fopen(OPS5_GAME_FILE, "r");
		if (g) {
			if (read_line(g, game, sizeof game) != 0)
				game[0] = '\0';
			fclose(g);
		}
		if (!game[0] && !first_game("/data/games", game, sizeof game) &&
		    !first_game("/mnt/usb0/games", game, sizeof game) && !first_game("/mnt/usb1/games", game, sizeof game)) {
			snprintf(line, sizeof line, "outsider-ps5: no game to start: no %s, no %s, no game folder in "
			         "/data/games or the USB games folders\n", OPS5_LAUNCH, OPS5_GAME_FILE);
			ops5_log(line);
			return 1;
		}
		game_root(game);
		escape_into(esc, sizeof esc, game);
		snprintf(home, sizeof home, "%s/outsider", OPS5_DATA_DIR);
		snprintf(env, sizeof env, "HOME=%s PS5PATH_CASE_INSENSITIVE=1", home);
		snprintf(args, sizeof args, "--game %s --shims " OPS5_SHIMS, esc);
		/* Diagnostics are off unless asked for, so a recording sees none of their cost: a file named diag in the
		   data folder turns on the per-120-frame perf line and SDL's GPU/video debug messages, profile adds the
		   sampling profiler */
		if (diag)
			strncat(args, " --perf 120", sizeof args - strlen(args) - 1);
		if (access(OPS5_DATA_DIR "/outsider/profile", F_OK) == 0)
			strncat(args, " --profile 5", sizeof args - strlen(args) - 1);
	}

	/* Logged now: splitting the arguments below cuts the line into words */
	snprintf(line, sizeof line, "outsider-ps5: starting Outsider on Vulkan (%s)\n", args);
	ops5_log(line);

	/* SDL's own messages about the GPU and the display (warnings are hidden by default), to the log */
	setenv("SDL_LOGGING", diag ? "gpu=debug,video=debug,render=info,*=warn" : "*=error", 0);

	char *words[64];
	int n = split_escaped(env, words, 64);
	for (int i = 0; i < n; i++) {
		char *eq = strchr(words[i], '=');
		if (!eq)
			continue;
		*eq = '\0';
		setenv(words[i], eq + 1, 1);
	}
	mkdir(OPS5_DATA_DIR, 0777);
	mkdir(home, 0777);
	chmod(home, 0777);
	if (chdir(home) != 0) {
		snprintf(line, sizeof line, "outsider-ps5: cannot switch into %s\n", home);
		ops5_log(line);
	}

	char *argv[66];
	argv[0] = "Outsider (Vulkan)";
	int argc = 1 + split_escaped(args, argv + 1, 64);
	argv[argc] = NULL;
	ops5_log_start_flusher(); /* Outsider logs a line per message, and a write to the log costs 50-150 ms here */
	return outsider_main(argc, argv);
}
