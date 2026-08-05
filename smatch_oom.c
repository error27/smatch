/*
 * Copyright (C) 2026 Dan Carpenter
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void set_oom_killer(void)
{
	char path[64];
	const char value[] = "500\n";
	int fd;

	snprintf(path, sizeof(path), "/proc/%ld/oom_score_adj", (long)getpid());

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed: open(%s): %s\n", path, strerror(errno));
		return;
	}

	write(fd, value, sizeof(value) - 1);
	close(fd);
}
