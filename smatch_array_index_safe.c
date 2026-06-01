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

static int definitely_just_used_as_limiter(struct expression *expr, struct expression *array, struct expression *offset)
{
	sval_t sval;
	struct expression *tmp;
	struct symbol *type;

	if (!get_implied_value(offset, &sval))
		return 0;
	if (get_array_size(array) != sval.value)
		return 0;

	type = get_type(expr);
	if (type && type->type == SYM_ARRAY)
		return 1;

	tmp = array;
	while ((tmp = expr_get_parent_expr(tmp))) {
		if (tmp->type == EXPR_PREOP && tmp->op == '&')
			return 1;
	}

	return 0;
}

static int common_false_positives(struct expression *array, sval_t max)
{
	char *name;
	int ret;

	array = strip_expr(array);
	name = expr_to_str(array);

	/* Smatch can't figure out glibc's strcmp __strcmp_cg()
	 * so it prints an error every time you compare to a string
	 * literal array with 4 or less chars.
	 */
	if (name &&
	    (strcmp(name, "__s1") == 0 || strcmp(name, "__s2") == 0)) {
		ret = 1;
		goto free;
	}

	/* Ugh... People are saying that Smatch still barfs on glibc strcmp()
	 * functions.
	 */
	if (array) {
		char *macro;

		/* why is this again??? */
		if (array->type == EXPR_STRING &&
		    max.value == array->string->length) {
			ret = 1;
			goto free;
		}

		macro = get_macro_name(array->pos);
		if (macro && max.uvalue < 4 &&
		    (strcmp(macro, "strcmp")  == 0 ||
		     strcmp(macro, "strncmp") == 0 ||
		     strcmp(macro, "streq")   == 0 ||
		     strcmp(macro, "strneq")  == 0 ||
		     strcmp(macro, "strsep")  == 0)) {
			ret = 1;
			goto free;
		}
	}

	/*
	 * passing WORK_CPU_UNBOUND is idiomatic but Smatch doesn't understand
	 * how it's used so it causes a bunch of false positives.
	 */
	if (option_project == PROJ_KERNEL && name &&
	    strcmp(name, "__per_cpu_offset") == 0) {
		ret = 1;
		goto free;
	}
	ret = 0;

free:
	free_string(name);
	return ret;
}

static unsigned long __TCA_FLOWER_MAX(void)
{
	struct symbol *sym;
	struct ident *id;
	sval_t sval;

	id = built_in_ident("__TCA_FLOWER_MAX");
	sym = lookup_symbol(id, NS_SYMBOL);
	if (!sym)
		return 0;
	if (!get_value(sym->initializer, &sval))
		return 0;
	return sval.value;
}

static bool is_out_of_sync_nla_tb(struct expression *array_expr, struct expression *offset)
{
	sval_t sval;
	char *type;

	if (option_project != PROJ_KERNEL)
		return false;

	if (!get_value(offset, &sval))
		return false;
	type = type_to_str(get_type(array_expr));
	if (!type)
		return false;
	if (strcmp(type, "struct nlattr**") != 0)
		return false;

	if (sval.uvalue >= __TCA_FLOWER_MAX())
		return false;

	return true;
}

static int constraint_met(struct expression *array_expr, struct expression *offset)
{
	char *data_str, *required, *unmet;
	int ret = 0;

	data_str = get_constraint_str(array_expr);
	if (!data_str)
		return 0;

	required = get_required_constraint(data_str);
	if (!required)
		goto free_data_str;

	unmet = unmet_constraint(array_expr, offset);
	if (!unmet)
		ret = 1;
	free_string(unmet);
	free_string(required);

free_data_str:
	free_string(data_str);
	return ret;
}

static bool is_zero_size_memcpy(struct expression *expr, int size, struct range_list *rl)
{
	struct expression *parent;

	/*
	 * Often times we have code like this:
	 * 	memcpy(array[idx], src, size)
	 * In this example if "idx == ARRAY_SIZE()" then "size" is zero so
	 * nothing is copied and the code is fine and Smatch should not
	 * print a warning even though the idx is one element out of bounds.
	 *
	 * TODO: if we wanted to be very accurate we could find the length
	 * expression and assume() that offset == rl_max() and then test that
	 * the length expression is zero.  But that seems like a lot of work.
	 * HashtagLazy.
	 */

	if (rl_max(rl).value != size)
		return false;

	parent = expr;
	while ((parent = expr_get_parent_expr(parent))) {
		if (parent->type == EXPR_PREOP &&
		    (parent->op == '(' || parent->op == '&'))
			continue;
		if (parent->type == EXPR_CAST)
			continue;
		break;
	}
	if (!parent || parent->type != EXPR_CALL ||
	    parent->fn->type != EXPR_SYMBOL || !parent->fn->symbol_name)
		return false;

	if (strstr(parent->fn->symbol_name->name, "memcpy") ||
	    strstr(parent->fn->symbol_name->name, "memset"))
		return true;

	return false;
}

bool array_safe(struct expression *array, struct expression *index)
{
	struct range_list *idx_rl;
	int size;

	/* NOTE: this function does not check for underflows */

	size = get_array_size(array);
	get_absolute_rl(index, &idx_rl);

	if (size > rl_max(idx_rl).uvalue)
		return true;

	if (constraint_met(array, index))
		return true;

	if (is_impossible_variable(index))
		return true;

	if (common_false_positives(array, rl_max(idx_rl)))
		return true;

	if (is_out_of_sync_nla_tb(array, index))
		return true;

	if (impossibly_high_comparison(index))
		return true;

	return false;
}

bool array_safe_expr(struct expression *expr)
{
	struct expression *array, *index;
	struct range_list *idx_rl;
	int size;

	if (!is_array(expr))
		return false;

	expr = strip_expr(expr);
	array = get_array_base(expr);
	index = get_array_offset(expr);

	if (!array || !index)
		return false;

	if (array_safe(array, index))
		return true;

	size = get_array_size(array);
	get_absolute_rl(index, &idx_rl);

	if (is_zero_size_memcpy(expr, size, idx_rl))
		return true;
	if (definitely_just_used_as_limiter(expr, array, index))
		return true;
	if (buf_comparison_index_ok(expr))
		return true;

	return false;
}

