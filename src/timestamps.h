/* vi: set et sw=4 ts=4 cino=t0,(0: */
/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of mission-control
 *
 * Copyright (C) 2008 Nokia Corporation. 
 *
 * Contact: Alberto Mardegan  <alberto.mardegan@nokia.com>
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/*
 * Macro to add/mark "timestamps" to program in a way that can be found
 * from LTT traces.  This helps in pinpointing when something interesting
 * starts or ends in the trace.
 * 
 * Use like this:
 *	#include "timestamps.h"
 * ...
 *	timestamp("something-interesting-start");
 * 
 * To enable timestamping when building the code in Scratchbox,
 * do this before build:
 *	export SBOX_EXTRA_COMPILER_ARGS="-DCREATE_TIMESTAMPS=1"
 * 
 * If you don't have LTT, you can see the timestamps when running
 * the software with:
 *	strace -f -tt -e open ./binary 2>&1 | grep /tmp/stamps|cut -d, -f1
 * ir if program is already running, give "-p PID" for strace
 * to attach to it instead.
 * 
 * Although less useful due to bad granularity, if you (re-)create
 * /tmp/stamps directory before the test:
 * 	rm -r /tmp/tests/
 * 	mkdir -p /tmp/tests
 * you can afterwards see the timestamps with 1 sec granularity
 * (and see their order):
 *	ls -clrt /tmp/stamps/|awk '{print $8, $9}'
 */
#ifndef TIMESTAMPS_H
#define TIMESTAMPS_H

#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef CREATE_TIMESTAMPS
# define timestamp(step) { int _tmp_fd; \
	_tmp_fd = open("/tmp/stamps/" __FILE__ ":" step, O_CREAT|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH); \
	if (_tmp_fd >=0) close(_tmp_fd); }
#else
# define timestamp(step)
#endif

#endif /* TIMESTAMPS_H */
