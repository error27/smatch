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

/*
 * The preempt_count is a counter as you might guess.  It's incrememented
 * if we call a spinlock and decremented when we unlock.  We can hold more
 * than one spin lock at a time so at first I thought that we should just
 * record "We started this function with a preempt count of zero and took
 * two locks so that's a difference of +2 or -2 if we dropped the locks."
 * But that didn't work and it turns out more complicated than I imagined.
 *
 * Locking generally is complicated.  I think printk() was complicated, but
 * I didn't really figure out what the issue is.  And then there is also the
 * issue of recursion so the lock count got way out of hand.
 *
 * My approach is to pull out the code that controls the return_states data
 * into this function.  Only handle simple things.  Locking functions are
 * basically always simple and under 10 lines.  If they take more than one
 * spinlock then something complicated is happening so ignore that.
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

STATE(add);
STATE(sub);
STATE(ignore);

struct preempt_info {
	const char *name;
	int type;
};

static struct preempt_info func_table[] = {
	{ "preempt_count_add",			PREEMPT_ADD },
	{ "__preempt_count_add",		PREEMPT_ADD },
	{ "local_bh_disable",			PREEMPT_ADD },
	{ "spin_lock",				PREEMPT_ADD },
	{ "spin_lock_nested",			PREEMPT_ADD },
	{ "_spin_lock",				PREEMPT_ADD },
	{ "_spin_lock_nested",			PREEMPT_ADD },
	{ "__spin_lock",			PREEMPT_ADD },
	{ "__spin_lock_nested",			PREEMPT_ADD },
	{ "raw_spin_lock",			PREEMPT_ADD },
	{ "_raw_spin_lock",			PREEMPT_ADD },
	{ "_raw_spin_lock_nested",		PREEMPT_ADD },
	{ "__raw_spin_lock",			PREEMPT_ADD },
	{ "spin_lock_irq",			PREEMPT_ADD },
	{ "_spin_lock_irq",			PREEMPT_ADD },
	{ "__spin_lock_irq",			PREEMPT_ADD },
	{ "_raw_spin_lock_irq",			PREEMPT_ADD },
	{ "spin_lock_irqsave",			PREEMPT_ADD },
	{ "_spin_lock_irqsave",			PREEMPT_ADD },
	{ "__spin_lock_irqsave",		PREEMPT_ADD },
	{ "_raw_spin_lock_irqsave",		PREEMPT_ADD },
	{ "__raw_spin_lock_irqsave",		PREEMPT_ADD },
	{ "spin_lock_irqsave_nested",		PREEMPT_ADD },
	{ "_spin_lock_irqsave_nested",		PREEMPT_ADD },
	{ "__spin_lock_irqsave_nested",		PREEMPT_ADD },
	{ "_raw_spin_lock_irqsave_nested",	PREEMPT_ADD },
	{ "spin_lock_bh",			PREEMPT_ADD },
	{ "_spin_lock_bh",			PREEMPT_ADD },
	{ "__spin_lock_bh",			PREEMPT_ADD },
	{ "task_rq_lock",			PREEMPT_ADD },
	{ "netif_tx_lock_bh",			PREEMPT_ADD },
	{ "mt76_tx_status_lock",		PREEMPT_ADD },

	{ "preempt_count_sub",			PREEMPT_SUB },
	{ "__preempt_count_sub",		PREEMPT_SUB },
	{ "local_bh_enable",			PREEMPT_SUB },
	{ "__local_bh_enable_ip",		PREEMPT_SUB },
	{ "spin_unlock",			PREEMPT_SUB },
	{ "_spin_unlock",			PREEMPT_SUB },
	{ "__spin_unlock",			PREEMPT_SUB },
	{ "raw_spin_unlock",			PREEMPT_SUB },
	{ "_raw_spin_unlock",			PREEMPT_SUB },
	{ "__raw_spin_unlock",			PREEMPT_SUB },
	{ "spin_unlock_irq",			PREEMPT_SUB },
	{ "_spin_unlock_irq",			PREEMPT_SUB },
	{ "__spin_unlock_irq",			PREEMPT_SUB },
	{ "_raw_spin_unlock_irq",		PREEMPT_SUB },
	{ "__raw_spin_unlock_irq",		PREEMPT_SUB },
	{ "spin_unlock_irqrestore",		PREEMPT_SUB },
	{ "_spin_unlock_irqrestore",		PREEMPT_SUB },
	{ "__spin_unlock_irqrestore",		PREEMPT_SUB },
	{ "_raw_spin_unlock_irqrestore",	PREEMPT_SUB },
	{ "__raw_spin_unlock_irqrestore",	PREEMPT_SUB },
	{ "spin_unlock_bh",			PREEMPT_SUB },
	{ "_spin_unlock_bh",			PREEMPT_SUB },
	{ "__spin_unlock_bh",			PREEMPT_SUB },
	{ "task_rq_unlock",			PREEMPT_SUB },
	{ "queue_request_and_unlock",		PREEMPT_SUB },
	{ "netif_tx_unlock_bh",			PREEMPT_SUB },
	{ "mt76_tx_status_unlock",		PREEMPT_SUB },
};

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct smatch_state *state;

	state = get_state(my_id, "preempt", NULL);
	if (state != &add && state != &sub)
		return;

	sql_insert_return_states(return_id, return_ranges,
			(state == &add) ? PREEMPT_ADD : PREEMPT_SUB,
			-1, "", "");
}

static void preempt_count_add(const char *fn, struct expression *expr, void *_index)
{
	struct smatch_state *state;

	__preempt_add();

	/* If the state is already set then ignore it */
	state = get_state(my_id, "preempt", NULL);
	if (state)
		set_state(my_id, "preempt", NULL, &ignore);
	else
		set_state(my_id, "preempt", NULL, &add);
}

static void preempt_count_sub(const char *fn, struct expression *expr, void *_index)
{
	struct smatch_state *state;

	__preempt_sub();

	/* If the state is already set then ignore it */
	state = get_state(my_id, "preempt", NULL);
	if (state)
		set_state(my_id, "preempt", NULL, &ignore);
	else
		set_state(my_id, "preempt", NULL, &sub);
}

static bool is_preempt_primitive(struct expression *expr)
{
	int i;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return false;

	if (expr->fn->type != EXPR_SYMBOL)
		return false;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (sym_name_is(func_table[i].name, expr->fn))
			return true;
	}

	return false;
}

static void select_return_add(struct expression *expr, int param, char *key, char *value)
{
	if (is_preempt_primitive(expr))
		return;
	preempt_count_add(NULL, NULL, NULL);
}

static void select_return_sub(struct expression *expr, int param, char *key, char *value)
{
	if (is_preempt_primitive(expr))
		return;
	preempt_count_sub(NULL, NULL, NULL);
}

void check_preempt_info(int id)
{
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (func_table[i].type == PREEMPT_ADD)
			add_function_hook(func_table[i].name, &preempt_count_add, NULL);
		else
			add_function_hook(func_table[i].name, &preempt_count_sub, NULL);
	}

	select_return_states_hook(PREEMPT_ADD, &select_return_add);
	select_return_states_hook(PREEMPT_SUB, &select_return_sub);

	add_split_return_callback(&match_return_info);
}
