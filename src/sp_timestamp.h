/*
 * Macro / inline function to add/mark "timestamps" to program in a
 * way that can be found from LTT traces.  This helps in pinpointing
 * when something interesting starts or ends in the trace. Strace can
 * be used to get timings for these measurement points.
 * 
 * Copyright (C) 2008 by Nokia Corporation
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * 
 * usage:
 *	#include "sp_timestamp.h"
 * ...
 *	sp_timestamp("something-interesting-started");
 * 
 * To enable timestamping when building the code in Scratchbox,
 * do this before build:
 *	export SBOX_EXTRA_COMPILER_ARGS="-DSP_TIMESTAMP_CREATE=1"
 * 
 * You can see the timestamps when running the software with:
 *	strace -f -tt -e trace=open,execve ./binary 2>&1 | grep /tmp/stamps
 * 
 * If the programs are already running, give the program IDs which
 * strace should attach to with the "-p PID" argument instead of
 * a program name.
 * 
 * 
 * Implementation differences to Federico's Gnome app stracing tool:
 *     http://www.gnome.org/~federico/news-2006-03.html#timeline-tools
 * - Lower level C-library functions are used instead of Glib
 * - Gets process name from /proc/PID/cmdline instead of Gtk
 *   specific functions (macro version uses just __FILE__)
 * - Uses open() syscall instead of access() because LTT doesn't
 *   record filename used by access() and because programs do
 *   use open() less (ptrace has its own overhead)
 * - Files are in /tmp which on Maemo is tmpfs (in RAM)
 */
#ifndef SP_TIMESTAMP_H
#define SP_TIMESTAMP_H

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef SP_TIMESTAMP_CREATE
# define sp_timestamp(step)
#else

# ifdef SP_TIMESTAMP_MACRO
/* low overhead macro adding code filename to measurement filename */
#  define sp_timestamp(step) { int _tmp_fd; \
	_tmp_fd = open("/tmp/stamps/" __FILE__ ":" step, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH); \
	if (_tmp_fd >=0) close(_tmp_fd); }

# else

#include <sys/types.h>
#include <string.h>
#include <assert.h>

/* higher overhead Linux specific function that adds program name
 */
static inline void sp_timestamp(const char *step)
{
#define SP_TIMESTAMP_PATH "/tmp/stamps/"
	int fd, count, offset;
	char filename[256], *cmdname;

	/* get process command name */
	snprintf(filename, sizeof(filename), "/proc/%d/cmdline", getpid());
	fd = open(filename, O_RDONLY);
	assert(fd >= 0);

	strcpy(filename, SP_TIMESTAMP_PATH);
	offset = sizeof(SP_TIMESTAMP_PATH)-1;
	count = read(fd, filename+offset, sizeof(filename)-offset);
	assert(count > 0);
	close(fd);

	/* take basename of the command name */
	cmdname = strrchr(filename+offset, '/');
	memmove(filename+offset, cmdname+1, strlen(cmdname));
	offset += strlen(filename+offset);
	
	/* add step name */
	assert(sizeof(filename) > offset + 1 + strlen(step) + 1);
	filename[offset++] = ':';
	filename[offset] = '\0';
	strcat(filename+offset, step);
	
	fd = open(filename, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	if (fd >= 0) {
		close(fd);
	}
#undef SP_TIMESTAMP_PATH
}
# endif

#endif

#endif /* SP_TIMESTAMP_H */
