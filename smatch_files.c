/*
 * Copyright (C) 2009 Dan Carpenter.
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

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "parse.h"
#include "smatch.h"

int open_data_file(const char *filename)
{
	int fd;
	char buf[256];

	fd = open(filename, O_RDONLY);
	if (fd >= 0)
		goto exit;
	if (!data_dir)
		goto exit;
	snprintf(buf, 256, "%s/%s", data_dir, filename);
	fd = open(buf, O_RDONLY);
exit:
	return fd;
}

struct token *get_tokens_file(const char *filename)
{
	int fd;
	struct token *token;

	if (option_no_data)
		return NULL;
	fd = open_data_file(filename);
	if (fd < 0)
		return NULL;
	token = tokenize(filename, fd, NULL, NULL);
	close(fd);
	return token;
}
