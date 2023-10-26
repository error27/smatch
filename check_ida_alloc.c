/*
 * Copyright (C) 2022 Christophe Jaillet.
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
 * The 'max' parameter of ida_alloc_max() and ida_alloc_range() is not interpreted
 * as it was in the deprecated ida_simple_get().
 *
 * The 'max' value in the new functions is inclusive but it was exclusive before.
 * So, in older code, this parameter was often a power of 2, such as 1 << 16, so
 * that the maximum possible value was 0xffff.
 *
 * Now a power of 2 value is spurious.
 */
#include <fcntl.h>
#include <unistd.h>
#include "parse.h"
#include "smatch.h"

static int my_id;

static bool implied_power_of_two(struct expression *expr)
{
	sval_t sval;

	if (!get_implied_value(expr, &sval))
		return false;
	if (!(sval.uvalue & (sval.uvalue - 1)))
		return true;
	return false;
}

static bool is_power_of_two(struct expression *expr)
{
	expr = strip_expr(expr);

	if (expr->type == EXPR_BINOP &&
	    expr->op == SPECIAL_LEFTSHIFT &&
	    is_power_of_two(expr->left))
		return true;

	if (implied_power_of_two(expr))
		return true;

	return false;
}

static void match_ida_alloc_max(const char *fn, struct expression *expr, void *_arg_nr)
{
	int arg_nr = PTR_INT(_arg_nr);
	struct expression *arg_expr;

	arg_expr = get_argument_from_call_expr(expr->args, arg_nr);
	arg_expr = strip_expr(arg_expr);

	if (is_power_of_two(arg_expr))
		sm_error("Calling %s() with a 'max' argument which is a power of 2. -1 missing?",
			 fn);
}

static void match_ida_alloc_range(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg_expr1, *arg_expr2;

	sval_t sval;

	arg_expr1 = get_argument_from_call_expr(expr->args, 1);
	arg_expr1 = strip_expr(arg_expr1);
	arg_expr2 = get_argument_from_call_expr(expr->args, 2);
	arg_expr2 = strip_expr(arg_expr2);

	if (!get_implied_value(arg_expr1, &sval))
		return;

	if (sval.uvalue == 1)
		return;

	if (is_power_of_two(arg_expr2))
		sm_error("Calling %s() with a 'max' argument which is a power of 2. -1 missing?",
			  fn);
}

void check_ida_alloc(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_hook("ida_alloc_max", &match_ida_alloc_max, INT_PTR(1));
	add_function_hook("ida_alloc_range", &match_ida_alloc_range, NULL);
}
