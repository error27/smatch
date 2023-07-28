/*
 * Copyright (C) 2022 Oracle.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(get_irq);

static void match_platform_get_irq(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_state(my_id, name, sym, &get_irq);
}

static bool assigned_zero(struct expression *expr)
{
	struct sm_state *sm, *tmp;
	struct expression *prev;

	sm = get_assigned_sm(expr);
	if (!sm)
		return false;
	FOR_EACH_PTR(sm->possible, tmp) {
		prev = tmp->state->data;
		if (expr_is_zero(prev))
			return true;
	} END_FOR_EACH_PTR(tmp);

	return false;
}

static bool is_part_of_select(struct expression *expr)
{
	struct expression *parent;
	sval_t sval;

	/* Code like "return irq ?: -EINVAL;" is wrong but harmless */

	parent = expr_get_parent_expr(expr);
	if (!parent)
		return false;
	if (parent->type != EXPR_CONDITIONAL &&
	    parent->type != EXPR_SELECT)
		return false;

	if (get_value(parent->cond_false, &sval) &&
	    sval.value >= -4095 &&
	    sval.value < 0)
		return true;

	return false;
}

static void match_condition(struct expression *expr)
{
	struct range_list *rl;

	if (!expr_has_possible_state(my_id, expr, &get_irq))
		return;

	if (get_implied_rl(expr, &rl) && rl_min(rl).value == 0)
		return;

	if (assigned_zero(expr))
		return;

	if (is_part_of_select(expr))
		return;

	sm_msg("warn: platform_get_irq() does not return zero");
}

void check_platform_get_irq_return(int id)
{
	my_id = id;

	add_function_param_key_hook_late("platform_get_irq", &match_platform_get_irq, -1, "$", NULL);
	add_function_param_key_hook_late("platform_get_irq_optional", &match_platform_get_irq, -1, "$", NULL);
	add_function_param_key_hook_late("platform_get_irq_byname", &match_platform_get_irq, -1, "$", NULL);
	add_function_param_key_hook_late("platform_get_irq_byname_optional", &match_platform_get_irq, -1, "$", NULL);
	add_modification_hook(my_id, &set_undefined);
	add_hook(&match_condition, CONDITION_HOOK);
}
