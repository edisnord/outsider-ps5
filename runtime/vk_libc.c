/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* libc functions the Vulkan program's libraries need and neither the console's system libraries nor the
 * payload SDK fork's platform layer define (the OpenGL title takes them from the SDK's libc.a, which this
 * program does not link: see vulkan/title-link.sh). */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int __real_access(const char *path, int mode);

/* shim/ps5path.c's faccessat() wrapper, after it has made the path absolute: access() is the platform's */
int __real_faccessat(int fd, const char *path, int mode, int flag)
{
	(void)flag;
	if (fd == AT_FDCWD || (path && path[0] == '/'))
		return __real_access(path, mode);
	errno = ENOSYS;
	return -1;
}

/* QuickJS's std and os modules (quickjs-libc), which Outsider does not offer to games: std.tmpfile(),
 * os.mkdtemp() and os.exec()'s gid option */
FILE *tmpfile(void)
{
	errno = ENOSYS;
	return NULL;
}

char *mkdtemp(char *template)
{
	(void)template;
	errno = ENOSYS;
	return NULL;
}

int setgid(gid_t gid)
{
	(void)gid;
	errno = EPERM;
	return -1;
}

/* SDL3's GTK check (core/unix/SDL_gtk.c), which falls back to getuid() and getgid() */
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	(void)ruid; (void)euid; (void)suid;
	errno = ENOSYS;
	return -1;
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	(void)rgid; (void)egid; (void)sgid;
	errno = ENOSYS;
	return -1;
}

/* FreeBSD's MB_CUR_MAX, as GNU libiconv (SDL3's SDL_iconv) calls it: the console's libc exports it only as
 * __mb_cur_max. libiconv sizes buffers with it, so the largest length of any locale is the safe answer. */
int ___mb_cur_max(void)
{
	return MB_LEN_MAX;
}
