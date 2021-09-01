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

static struct void_fn_list *hooks;
static int my_id;

STATE(sleep);

#define __GFP_ATOMIC 0x200u
#define ___GFP_DIRECT_RECLAIM 0x400u

unsigned long GFP_DIRECT_RECLAIM(void)
{
	static unsigned long saved_flags = -1;
	struct symbol *macro_sym;

	if (saved_flags != -1)
		return saved_flags;

	macro_sym = lookup_macro_symbol("___GFP_DIRECT_RECLAIM");
	if (!macro_sym || !macro_sym->expansion)
		return ___GFP_DIRECT_RECLAIM;
	if (token_type(macro_sym->expansion) != TOKEN_NUMBER)
		return ___GFP_DIRECT_RECLAIM;

	saved_flags = strtoul(macro_sym->expansion->number, NULL, 0);
	return saved_flags;
}

unsigned long GFP_ATOMIC(void)
{
	static unsigned long saved_flags = -1;
	struct symbol *macro_sym;

	if (saved_flags != -1)
		return saved_flags;

	macro_sym = lookup_macro_symbol("___GFP_ATOMIC");
	if (!macro_sym || !macro_sym->expansion)
		return __GFP_ATOMIC;
	if (token_type(macro_sym->expansion) != TOKEN_NUMBER)
		return __GFP_ATOMIC;

	saved_flags = strtoul(macro_sym->expansion->number, NULL, 0);
	return saved_flags;
}

static void do_sleep(void)
{
	void_fn *fn;

	FOR_EACH_PTR(hooks, fn) {
		fn();
	} END_FOR_EACH_PTR(fn);

	set_state(my_id, "sleep", NULL, &sleep);
	clear_preempt_cnt();
}

static void match_sleep(const char *fn, struct expression *expr, void *unused)
{
	do_sleep();
}

static void match_might_sleep_fn(const char *fn, struct expression *expr, void *unused)
{
	struct range_list *rl;
	sval_t sval;

	if (!get_implied_rl_from_call_str(expr, "$0", &rl))
		return;
	if (!rl_to_sval(rl, &sval) || sval.value != 0)
		return;

	do_sleep();
}

static void match_might_sleep_macro(struct statement *stmt)
{
	const char *macro;

	macro = get_macro_name(stmt->pos);
	if (!macro ||
	    strcmp(macro, "might_sleep") != 0)
		return;

	do_sleep();
}

static void select_sleep(struct expression *call, struct expression *arg, char *key, char *unused)
{
	do_sleep();
}

static void match_gfp_t(struct expression *expr)
{
	struct expression *arg;
	char *name;
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
	if (sval.uvalue & GFP_ATOMIC())
		return;

	name = expr_to_str(expr->fn);
	if (name &&
	    (strncmp(name, "__xa_", 5) == 0 ||
	     strncmp(name, "xa_", 3) == 0 ||
	     strcmp(name, "ttm_bo_swapout") == 0)) {
		free_string(name);
		return;
	}
	free_string(name);

	do_sleep();
}

static void insert_sleep(void)
{
	if (function_decrements_preempt())
		return;
	if (get_state(my_id, "sleep", NULL) != &sleep)
		return;
	sql_insert_return_implies(SLEEP, -1, "", "");
}

void add_sleep_callback(void_fn *fn)
{
	add_ptr_list(&hooks, fn);
}

void check_sleep_info(int id)
{
	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_function_hook("schedule", &match_sleep, NULL);
	add_function_hook("might_resched", &match_sleep, NULL);
	add_function_hook("__might_sleep", &match_might_sleep_fn, NULL);
	add_function_hook("___might_sleep", &match_might_sleep_fn, NULL);
	add_hook(&match_might_sleep_macro, STMT_HOOK);

	add_hook(&match_gfp_t, FUNCTION_CALL_HOOK);

	all_return_states_hook(&insert_sleep);
	select_return_implies_hook(SLEEP, &select_sleep);
}
