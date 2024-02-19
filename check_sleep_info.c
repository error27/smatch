/*
 * Copyright (C) 2021 Oracle.
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

static struct expr_fn_list *hooks;

static int my_id;

STATE(sleep);

unsigned long GFP_DIRECT_RECLAIM(void)
{
	static unsigned long saved_flags = -1;
	struct symbol *macro_sym;

	if (saved_flags != -1)
		return saved_flags;

	macro_sym = lookup_macro_symbol("___GFP_DIRECT_RECLAIM");
	if (!macro_sym || !macro_sym->expansion)
		return 0;
	if (token_type(macro_sym->expansion) != TOKEN_NUMBER)
		return 0;

	saved_flags = strtoul(macro_sym->expansion->number, NULL, 0);
	return saved_flags;
}

static void do_sleep(struct expression *expr)
{
	call_expr_fns(hooks, expr);

	if (!function_decrements_preempt())
		set_state(my_id, "sleep", NULL, &sleep);
	clear_preempt_cnt();
	clear_irq_context();
}

static void match_sleep(const char *fn, struct expression *expr, void *unused)
{
	do_sleep(expr);
}

static void match_might_sleep_fn(const char *fn, struct expression *expr, void *unused)
{
	struct range_list *rl;
	sval_t sval;

	if (!get_implied_rl_from_call_str(expr, "$0", &rl))
		return;
	if (!rl_to_sval(rl, &sval) || sval.value != 0)
		return;

	do_sleep(expr);
}

static void match_might_sleep_macro(struct statement *stmt)
{
	const char *macro;

	macro = get_macro_name(stmt->pos);
	if (!macro ||
	    strcmp(macro, "might_sleep") != 0)
		return;

	do_sleep(NULL);
}

static void select_sleep(struct expression *call, struct expression *arg, char *key, char *unused)
{
	do_sleep(call);
}

static bool is_strange_GFP_function(struct expression *expr)
{
	char *name;
	bool ret = false;

	if (!expr || expr->type != EXPR_CALL)
		return false;

	name = expr_to_str(expr->fn);
	if (!name)
		return false;

	if (strncmp(name, "__xa_", 5) == 0 ||
	    strncmp(name, "xa_", 3) == 0 ||
	    strcmp(name, "ttm_bo_swapout") == 0 ||
	    strcmp(name, "mas_store_gfp") == 0 ||
	    strcmp(name, "mas_alloc_cyclic") == 0)
		ret = true;

	free_string(name);
	return ret;
}

static void match_gfp_t(struct expression *expr)
{
	struct expression *arg;
	sval_t sval;
	int param;

	param = get_gfp_param(expr);
	if (param < 0)
		return;
	arg = get_argument_from_call_expr(expr->args, param);
	if (!get_implied_value(arg, &sval))
		return;

	if (!(sval.value & GFP_DIRECT_RECLAIM()))
		return;

	if (is_strange_GFP_function(expr))
		return;

	do_sleep(expr);
}

static void insert_sleep(void)
{
	if (get_state(my_id, "sleep", NULL) != &sleep)
		return;
	sql_insert_return_implies(SLEEP, -2, "", "");
}

void add_sleep_callback(expr_func *fn)
{
	add_ptr_list(&hooks, fn);
}

void check_sleep_info(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_hook("schedule", &match_sleep, NULL);
	add_function_hook("msleep", &match_sleep, NULL);
	add_function_hook("might_resched", &match_sleep, NULL);
	add_function_hook("vfree", &match_sleep, NULL);
	add_function_hook("__might_sleep", &match_might_sleep_fn, NULL);
	add_function_hook("___might_sleep", &match_might_sleep_fn, NULL);
	add_hook(&match_might_sleep_macro, STMT_HOOK);

	add_hook(&match_gfp_t, FUNCTION_CALL_HOOK);

	all_return_states_hook(&insert_sleep);
	select_return_implies_hook_early(SLEEP, &select_sleep);
}
