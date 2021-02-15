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
	char buf[256];
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd >= 0)
		return fd;
	if (!data_dir)
		return -1;
	snprintf(buf, 256, "%s/%s", data_dir, filename);
	return open(buf, O_RDONLY);
}

int open_schema_file(const char *schema)
{
	char buf[256];
	int fd;

	fd = open_data_file(schema);
	if (fd >= 0)
		return fd;
	snprintf(buf, 256, "%s/smatch_data/%s", bin_dir, schema);
	return open(buf, O_RDONLY);
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
	token = tokenize(NULL, filename, fd, NULL, NULL);
	close(fd);
	return token;
}

struct string_list *load_strings_from_file(const char *project, const char *filename)
{
	struct string_list *ret = NULL;
	struct token *token;
	char buf[64];
	char *str;

	if (project)
		snprintf(buf, sizeof(buf), "%s.%s", project, filename);
	else
		snprintf(buf, sizeof(buf), "%s", filename);

	token = get_tokens_file(buf);
	if (!token)
		return NULL;

	if (token_type(token) != TOKEN_STREAMBEGIN)
		return NULL;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			break;
		str = alloc_string(show_ident(token->ident));
		add_ptr_list(&ret, str);
		token = token->next;
	}
	clear_token_alloc();

	return ret;
}
