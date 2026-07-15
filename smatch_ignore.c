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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "smatch.h"

STATE(ignore);
static struct stree *ignored;
static struct stree *ignored_from_file;

void add_ignore(int owner, const char *name, struct symbol *sym)
{
	if (!output_enabled)
		return;
	set_state_stree(&ignored, owner, name, sym, &ignore);
}

int is_ignored(int owner, const char *name, struct symbol *sym)
{
	return !!get_state_stree(ignored, owner, name, sym);
}

void add_ignore_expr(int owner, struct expression *expr)
{
	struct symbol *sym;
	char *name;

	name = expr_to_str_sym(expr, &sym);
	if (!name || !sym)
		return;
	add_ignore(owner, name, sym);
	free_string(name);
}

int is_ignored_expr(int owner, struct expression *expr)
{
	struct symbol *sym;
	char *name;
	int ret;

	name = expr_to_str_sym(expr, &sym);
	if (!name && !sym)
		return 0;
	ret = is_ignored(owner, name, sym);
	free_string(name);
	if (ret)
		return true;

	name = get_macro_name(expr->pos);
	if (name && get_state_stree(ignored_from_file, owner, name, NULL))
		return true;

	name = get_function();
	if (name && get_state_stree(ignored_from_file, owner, name, NULL))
		return true;

	return false;
}

bool ignored_warning(const char *check_name)
{
	char *macro = NULL;
	int owner = id_from_name(check_name);

	if (option_info)
		return false;
	if (owner < 0)
		return false;

	if (get_state_stree(ignored_from_file, owner, get_filename(), NULL))
		return true;

	if (get_state_stree(ignored_from_file, owner, get_function(), NULL))
		return true;

	if (__hook_pos) {
		macro = get_macro_name(*__hook_pos);
		if (get_state_stree(ignored_from_file, owner, macro, NULL))
			return true;
	}

	return false;
}

static void clear_ignores(void)
{
	if (__inline_fn)
		return;
	free_stree(&ignored);
}

static void load_ignore_file(const char *ignore_file)
{
	struct token *token;
	const char *str;
	int owner;
	char check_name[64];
	char buf[64];
	char *p;

	p = strstr(ignore_file, ".ignore");
	snprintf(check_name, sizeof(check_name), "%.*s", (int)(p - ignore_file),
		 ignore_file);
	owner = id_from_name(check_name);
	if (owner < 0)
		return;

	snprintf(buf, sizeof(buf), "%s/%s", option_project_str, ignore_file);
	token = get_tokens_file(buf);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			break;
		str = show_ident(token->ident);
		token = token->next;

		set_state_stree_perm(&ignored_from_file, owner, str, NULL, &ignore);
	}
	clear_token_alloc();
}

static void load_ignore_files(void)
{
	struct dirent *entry;
	char buf[64];
	DIR *dir;


	snprintf(buf, sizeof(buf), "%s/%s/", data_dir, option_project_str);
	dir = opendir(buf);
	if (!dir)
		return;

	while ((entry = readdir(dir))) {
		if (entry->d_type != DT_REG)
			continue;

		if (strncmp(entry->d_name, "check_", 6) != 0 ||
		    !strstr(entry->d_name, ".ignore"))
			continue;
		load_ignore_file(entry->d_name);
	}

	closedir(dir);
}

void smatch_smatch_ignore(int id)
{
	add_hook(&clear_ignores, AFTER_FUNC_HOOK);
	load_ignore_files();
}
