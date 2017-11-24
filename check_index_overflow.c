/*
 * Copyright (C) 2010 Dan Carpenter.
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

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int definitely_just_used_as_limiter(struct expression *array, struct expression *offset)
{
	sval_t sval;
	struct expression *tmp;
	int step = 0;
	int dot_ops = 0;

	if (!get_implied_value(offset, &sval))
		return 0;
	if (get_array_size(array) != sval.value)
		return 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (step == 0) {
			step = 1;
			continue;
		}
		if (tmp->type == EXPR_PREOP && tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
		if (step == 1 && tmp->op == '&') {
			step = 2;
			continue;
		}
		if (step == 2 && tmp->type == EXPR_COMPARE)
			return 1;
		if (step == 2 && tmp->type == EXPR_ASSIGNMENT)
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

static int get_the_max(struct expression *expr, sval_t *sval)
{
	struct range_list *rl;

	if (get_hard_max(expr, sval))
		return 1;
	if (!option_spammy)
		return 0;
	if (get_fuzzy_max(expr, sval))
		return 1;
	if (!get_user_rl(expr, &rl))
		return 0;
	*sval = rl_max(rl);
	return 1;
}

static int common_false_positives(struct expression *array, char *name, sval_t max)
{
	if (!name)
		return 0;

	/* Smatch can't figure out glibc's strcmp __strcmp_cg()
	 * so it prints an error every time you compare to a string
	 * literal array with 4 or less chars.
	 */
	if (strcmp(name, "__s1") == 0 || strcmp(name, "__s2") == 0)
		return 1;

	/* Ugh... People are saying that Smatch still barfs on glibc strcmp()
	 * functions.
	 */
	if (array) {
		char *macro;

		/* why is this again??? */
		if (array->type == EXPR_STRING &&
		    max.value == array->string->length)
			return 1;

		macro = get_macro_name(array->pos);
		if (macro && max.uvalue < 4 &&
		    (strcmp(macro, "strcmp")  == 0 ||
		     strcmp(macro, "strncmp") == 0 ||
		     strcmp(macro, "streq")   == 0 ||
		     strcmp(macro, "strneq")  == 0 ||
		     strcmp(macro, "strsep")  == 0))
			return 1;
	}

	/*
	 * passing WORK_CPU_UNBOUND is idiomatic but Smatch doesn't understand
	 * how it's used so it causes a bunch of false positives.
	 */
	if (option_project == PROJ_KERNEL &&
	    strcmp(name, "__per_cpu_offset") == 0)
		return 1;
	return 0;
}

static void array_check(struct expression *expr)
{
	struct expression *array_expr;
	const char *level = "error";
	int array_size;
	struct expression *offset;
	sval_t max;
	char *name;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	if (is_impossible_path())
		return;
	array_expr = get_array_base(expr);
	array_size = get_array_size(array_expr);
	if (!array_size || array_size == 1)
		return;

	offset = get_array_offset(expr);
	if (!get_the_max(offset, &max))
		return;
	if (array_size > max.value)
		return;
	if (getting_address())
		level = "warn";

	if (definitely_just_used_as_limiter(array_expr, offset))
		return;

	array_expr = strip_expr(array_expr);
	name = expr_to_str(array_expr);
	if (common_false_positives(array_expr, name, max))
		goto free;

	sm_msg("%s: buffer overflow '%s' %d <= %s",
		level, name, array_size, sval_to_str(max));

free:
	free_string(name);
}

void check_index_overflow(int id)
{
	add_hook(&array_check, OP_HOOK);
}
