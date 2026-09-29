/*
 * Copyright (C) 2026 Dan Carpenter.
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

struct expects_nul {
	const char *fn;
	int param;
	const char *key;
};

static struct expects_nul expects_nul[] = {
	{ "kstrtobool", 0, "$" },
	{ "kstrtoint", 0, "$" },
	{ "kstrtol", 0, "$" },
	{ "kstrtoll", 0, "$" },
	{ "kstrtos16", 0, "$" },
	{ "kstrtos32", 0, "$" },
	{ "kstrtos64", 0, "$" },
	{ "kstrtos8", 0, "$" },
	{ "kstrtou16", 0, "$" },
	{ "kstrtou32", 0, "$" },
	{ "kstrtou64", 0, "$" },
	{ "kstrtou8", 0, "$" },
	{ "kstrtouint", 0, "$" },
	{ "kstrtoul", 0, "$" },
	{ "kstrtoull", 0, "$" },
	{ "sscanf", 0, "$" },
	{ "strcasecmp", 0, "$" },
	{ "strcasecmp", 1, "$" },
	{ "strcat", 0, "$" },
	{ "strcat", 1, "$" },
	{ "strchr", 0, "$" },
	{ "strcmp", 0, "$" },
	{ "strcmp", 1, "$" },
	{ "strcpy", 1, "$" },
	{ "strdup", 0, "$" },
	{ "strlen", 0, "$" },
	{ "strstr", 0, "$" },
	{ "strstr", 1, "$" },
};

static void match_expects_nul(struct expression *string)
{
	char *string_name;

	if (!is_unterminated_user_string(string)) {
		string = strip_expr(string);
		if (!string || string->type != EXPR_BINOP || string->op != '+' ||
		    !is_pointer(string->left) ||
		    !is_unterminated_user_string(string->left))
			return;
		string = string->left;
	}

	string_name = expr_to_str(string);
	sm_warning("unterminated user string: '%s'", string_name);
	free_string(string_name);
}

static void match_all_expects_nul(const char *fn, struct expression *expr,
				  void *unused)
{
	struct expression *arg;

	FOR_EACH_PTR(expr->args, arg) {
		match_expects_nul(arg);
	} END_FOR_EACH_PTR(arg);
}

void check_unterminated_string(int id)
{
	struct expects_nul *info;
	int i;

	if (option_project != PROJ_KERNEL)
		return;

	for (i = 0; i < ARRAY_SIZE(expects_nul); i++) {
		info = &expects_nul[i];
		add_param_key_expr_hook(info->fn, &match_expects_nul, info->param,
					info->key, info);
	}

	add_function_hook("dev_info", &match_all_expects_nul, NULL);
	add_function_hook("printk", &match_all_expects_nul, NULL);
	add_function_hook("pr_info", &match_all_expects_nul, NULL);
	add_function_hook("snprintf", &match_all_expects_nul, NULL);
	add_function_hook("sprintf", &match_all_expects_nul, NULL);
}
