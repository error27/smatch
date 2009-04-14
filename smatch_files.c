/*
 * sparse/smatch_files.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "parse.h"
#include "smatch.h"

static int open_file(const char *filename)
{
	int fd;
	char *buf = malloc(256);

	fd = open(filename, O_RDONLY);
	if (fd >= 0)
		goto exit;
	if (!data_dir)
		goto exit;
	strncpy(buf, data_dir, 254);
	buf[255] = '\0';
	strncat(buf, filename, 254 - strlen(buf));
	fd = open(buf, O_RDONLY);
	if (fd >= 0)
		goto exit;

exit:
	free(buf);
	return fd;
}

struct token *get_tokens_file(const char *filename)
{
	int fd;
	struct token *token;

	if (option_no_data)
		return NULL;
	fd = open_file(filename);
	if (fd < 0)
		return NULL;
	token = tokenize(filename, fd, NULL, NULL);
	close(fd);
	return token;
}
