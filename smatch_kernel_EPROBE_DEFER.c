/*
 * Copyright 2023 Linaro Ltd.
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
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(eprobe_defer);

#define EPROBE_DEFER_VAL 517

sval_t eprobe_defer_sval = {
	.type = &int_ctype,
	.value = -EPROBE_DEFER_VAL,
};

bool is_EPROBE_DEFER_literal(struct expression *expr)
{
	char *macro;
	sval_t sval;

	if (!expr)
		return false;

	if (expr->type != EXPR_PREOP || expr->op != '-')
		return false;

	expr = expr->unop;

	if (!get_value(expr, &sval) || sval.value != EPROBE_DEFER_VAL)
		return false;

	macro = get_macro_name(expr->pos);
	if (!macro || strcmp(macro, "EPROBE_DEFER") != 0)
		return false;

	return true;
}

bool is_EPROBE_DEFER(struct expression *expr)
{
	struct range_list *rl;

	if (!expr)
		return false;

	if (is_EPROBE_DEFER_literal(expr))
		return true;

	if (!expr_has_possible_state(my_id, expr, &eprobe_defer))
		return false;

	get_absolute_rl(expr, &rl);
	if (!rl_intersection(rl, alloc_rl(eprobe_defer_sval, eprobe_defer_sval)))
		return false;

	return true;
}

bool is_EPROBE_DEFER_name_sym(const char *name, struct symbol *sym)
{
	struct range_list *rl;
	struct smatch_state *estate;

	estate = get_state(SMATCH_EXTRA, name, sym);
	if (!estate)
		return true;

	if (!rl_intersection(estate_rl(estate), alloc_rl(eprobe_defer_sval, eprobe_defer_sval)))
		return false;

	return true;
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	if (!is_EPROBE_DEFER(expr))
		return;

	sql_insert_return_states(return_id, return_ranges, EPROBE_DEFER, -1, "$", "");
}

static void match_assign(struct expression *expr)
{
	if (expr->op != '=')
		return;
	if (!has_states(__get_cur_stree(), my_id))
		return;
	if (is_EPROBE_DEFER(expr->right))
		set_state_expr(my_id, expr->left, &eprobe_defer);
}

static void set_eprobe_defer(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &eprobe_defer);
}

void register_kernel_EPROBE_DEFER(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_modification_hook(my_id, &set_undefined);
	add_hook(&match_assign, ASSIGNMENT_HOOK);

	add_split_return_callback(match_return_info);
	select_return_param_key(EPROBE_DEFER, &set_eprobe_defer);
}
