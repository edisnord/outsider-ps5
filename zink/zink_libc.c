/*
 * libc functions a program with Mesa's GL and Zink imports that only libScePosixForWebKit's link stub defines.
 * That module gives a game process nothing: the import stays null and a call jumps to a bad address (XashPS5 met it
 * at launch, PS5_Vulkan's tools/radv-link.sh has the same note for strcasestr and mkstemp). The pinned SDK fork's
 * platform layer has mkstemps and no mkstemp, so these supply them in the program; the link keeps the names local.
 *
 *   isatty    Mesa's ARB program lexer (flex), libc++'s print.cpp
 *   mkstemp   ACO's disassembly dump (aco_print_asm.cpp)
 *
 * Copyright (C) 2026 the Outsider PS5 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

/* No terminals on the console */
int isatty(int fd)
{
	(void)fd;
	errno = ENOTTY;
	return 0;
}

/* radv-link.sh binds mkstemps to the platform layer's ps5_mkstemps */
int mkstemp(char *template)
{
	return mkstemps(template, 0);
}
