/*
 * Copyright (C) 2020 Oracle.
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

#include "smatch.h"

static int my_id;

static unsigned long put_device_stmt;

bool was_put_device_stmt(void)
{
	return put_device_stmt;
}

static void clear_put_device_stmt(struct statement *stmt)
{
	put_device_stmt = false;
}

static int save_string(void *_list, int argc, char **argv, char **azColName)
{
	struct string_list **list = _list;

	if (argc != 1)
		return 0;

	sm_msg("argv[0] = %s", argv[0]);

	insert_string(list, alloc_string(argv[0]));
	return 0;
}

static struct expression *guess_release_fn(void)
{
	struct string_list *list = NULL;

	/* This function is trickier than I imagined */
	return NULL;

	if (get_file_id() != get_base_file_id())
		return NULL;

	run_sql(save_string, &list,
		"select function from function_ptr where file = 0x%llx and \
			(ptr = '(struct device)->release' or 		   \
			 ptr = '(struct device_type)->release' or	   \
			 ptr = '(struct class)->dev_release');",
		get_base_file_id());

//	return list;
	return NULL;
}

static char *release_name(const char *name, const char *fn_ptr)
{
	char buf[64];

	if (name[0] == '&')
		snprintf(buf, sizeof(buf), "%s.%s", name + 1, fn_ptr);
	else
		snprintf(buf, sizeof(buf), "%s->%s", name, fn_ptr);

	return alloc_sname(buf);
}

static struct expression *get_release_fn(const char *name, struct symbol *sym)
{
	struct expression *fn;
	const char *release;

	release = release_name(name, "release");
	fn = get_assigned_expr_name_sym(release, sym);
	if (fn)
		return fn;

	release = release_name(name, "type->release");
	fn = get_assigned_expr_name_sym(release, sym);
	if (fn)
		return fn;

	release = release_name(name, "class->dev_release");
	fn = get_assigned_expr_name_sym(release, sym);
	if (fn)
		return fn;

	return guess_release_fn();
}

static void match_put_device_fake(struct expression *expr, const char *name, struct symbol *sym)
{
	struct expression *fn, *arg, *fake_call;
	struct expression_list *args = NULL;
	sval_t sval;

	fn = get_release_fn(name, sym);
	if (!fn)
		return;
	if (get_implied_value(fn, &sval) && sval.value == 0)
		return;
	if (fn->type == EXPR_PREOP && fn->op == '&')
		fn = strip_expr(fn->unop);

	/*
	 * I had imagined this module could just consume information from
	 * smatch_kernel_put_device_info.c.  But it turns out the once we
	 * handle it then we have mark it as ignored to avoid double free
	 * warnings.
	 */
	set_ignore_put_device(name, sym);

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;

	add_ptr_list(&args, arg);
	fake_call = call_expression(fn, args);
	__split_expr(fake_call);
	put_device_stmt = true;
}

void smatch_kernel_put_device(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_data(&put_device_stmt);

	add_put_device_hook(&match_put_device_fake);
	add_hook(&clear_put_device_stmt, STMT_HOOK);
}

