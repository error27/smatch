/*
 * sparse/smatch_oom.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int option_oom_kb = 800000;  // 800 megs

static long get_mem_used()
{
	static char filename[64];
	static char buf[4096];
	int fd;
	char *line;
	
	if (!filename[0]) {
		snprintf(filename, 63, "/proc/%d/status", getpid());
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -1;
	if (read(fd, buf, 4096) < 0) {
		close(fd);
		return -1;
	}
	close(fd);

	line = buf;
	while ((line = strchr(line, 'V')) > 0) {
		if (!strncmp(line, "VmSize:", 7))
			return atol(line + 8);
		line++;
	}
	return -1;
}

int out_of_memory()
{
	if (get_mem_used() > option_oom_kb)
		return 1;
	return 0;
}

