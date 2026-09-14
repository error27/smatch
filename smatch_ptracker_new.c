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

/*
 * An stracker looks a lot like and mtag, but imagine if a pointer is
 * either an mtag or NULL, with an mtag it's just one number.  If you
 * merge to pointers together, then you get one stracker number.
 *
 * But it's like mtags because we have a number and an offset.
 * And sometimes it could actually be an mtag.  Why not?
 *
 * This is mostly focused on parameters and returns.  But also I guess
 * struct member assignments.
 *
 */

#include "smatch.h"

static int my_id;

static sval_t str_to_new_ptracker(int type, const char *str)
{
	sval_t ret = { .type = &ullong_ctype };

	ret.value = str_to_mtag(str);
	sql_insert_ptracker(ret.value, type, str);
	return ret;
}

static bool alloc_new_ptracker(struct expression *expr, sval_t *sval)
{
	struct expression *tmp;
	const char *key;
	char buf[128];
	char *str;
	int param;

	param = get_param_key_from_expr(expr, NULL, &key);
	if (param >= 0 && !param_was_set(expr)) {
		if (strcmp(key, "$") == 0) {
			snprintf(buf, sizeof(buf), "%d,0x%llx,%s,%d",
				 param,
				 is_local(cur_func_sym) ? get_base_file_id() : 0,
				 get_function(), is_local(cur_func_sym));
			*sval = str_to_new_ptracker(PTRACKER, buf);
		} else {
			snprintf(buf, sizeof(buf), "%s:%d %s() %d %s",
				 get_filename(), get_lineno(), get_function(),
				 param, key);
			*sval = str_to_new_ptracker(PARAM_VALUE, buf);
		}
		return true;
	}

	tmp = get_assigned_expr_recurse(expr);
	if (tmp)
		expr = tmp;

	str = expr_to_str(expr);
	if (!str)
		return false;

	snprintf(buf, sizeof(buf), "%s:%d %s() %s",
		 get_filename(), get_lineno(), get_function(), str);

	*sval = str_to_new_ptracker(PARAM_VALUE, buf);
	return true;
}

static void match_call_info(struct expression *expr)
{
	struct expression *arg;
	sval_t sval;
	int param;

	param = -1;
	FOR_EACH_PTR(expr->args, arg) {
		param++;

		/* if we already have a tracker then use that */
		if (get_old_ptracker(arg, &sval)) {
			sql_insert_caller_info(expr, PTRACKER, param, "$",
					       sval_to_str(sval));
			continue;
		}

		/* otherwise generate a tracker */
		if (alloc_new_ptracker(arg, &sval)) {
			sql_insert_caller_info(expr, PTRACKER, param, "$",
					       sval_to_str(sval));
			continue;
		}
	} END_FOR_EACH_PTR(arg);
}

void smatch_ptracker_new(int id)
{
	if (!option_info)
		return;

	my_id = id;

	add_hook(&match_call_info, FUNCTION_CALL_HOOK);
}
