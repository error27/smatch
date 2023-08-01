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
 * This is basically a copy for check_preempt_info.c but for refcount instead.
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

STATE(init);
STATE(inc);
STATE(dec);
STATE(ignore);
STATE(gone);

struct ref_func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	param_key_hook *call_back;
};

static void match_atomic_add(struct expression *expr, const char *name, struct symbol *sym, void *_unused);

static struct ref_func_info func_table[] = {
	{ "atomic_inc", REFCOUNT_INC, 0, "$->counter" },
	{ "atomic_long_inc", REFCOUNT_INC, 0, "$->counter" },
	{ "atomic64_inc", REFCOUNT_INC, 0, "$->counter" },

	{ "atomic_inc_return", REFCOUNT_INC, 0, "$->counter" },
	{ "atomic_long_inc_return", REFCOUNT_INC, 0, "$->counter" },
	{ "atomic64_return", REFCOUNT_INC, 0, "$->counter" },

	{ "atomic_add_return", REFCOUNT_INC, 1, "$->counter", NULL, NULL, match_atomic_add },
	{ "atomic_long_add_return", REFCOUNT_INC, 1, "$->counter", NULL, NULL, match_atomic_add },
	{ "atomic64_add_return", REFCOUNT_INC, 1, "$->counter", NULL, NULL, match_atomic_add },

	{ "atomic64_inc_not_zero", REFCOUNT_INC, 0, "$->counter", &bool_true, &bool_true},
	{ "atomic64_fetch_add_unless", REFCOUNT_INC, 0, "$->counter", &bool_true, &bool_true},
	{ "atomic64_add_unless", REFCOUNT_INC, 0, "$->counter", &bool_true, &bool_true},
	{ "atomic64_add_unless_negative", REFCOUNT_INC, 0, "$->counter", &bool_true, &bool_true},
	{ "atomic64_add_unless_positive", REFCOUNT_INC, 0, "$->counter", &bool_true, &bool_true},

	// atomic64_dec_if_positive
	// atomic64_dec_unless_positive

	{ "atomic_dec", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic_long_dec", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic64_dec", REFCOUNT_DEC, 0, "$->counter" },

	{ "atomic_dec_return", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic_long_dec_return", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic64_dec_return", REFCOUNT_DEC, 0, "$->counter" },

	{ "atomic_dec_and_test", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic_long_dec_and_test", REFCOUNT_DEC, 0, "$->counter" },
	{ "atomic64_dec_and_test", REFCOUNT_DEC, 0, "$->counter" },

	{ "_atomic_dec_and_lock", REFCOUNT_DEC, 0, "$->counter" },

	{ "atomic_sub", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic_long_sub", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic64_sub", REFCOUNT_DEC, 1, "$->counter" },

	{ "atomic_sub_return", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic_long_sub_return", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic64_sub_return", REFCOUNT_DEC, 1, "$->counter" },

	{ "atomic_sub_and_test", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic_long_sub_and_test", REFCOUNT_DEC, 1, "$->counter" },
	{ "atomic64_sub_and_test", REFCOUNT_DEC, 1, "$->counter" },

	{ "register_device", REFCOUNT_INIT, 0, "$->kobj.kset->kobj.kref.refcount.refs.counter", &err_min, &err_max},
	{ "register_device", REFCOUNT_INC, 0, "$->kobj.kset->kobj.kref.refcount.refs.counter", &int_zero, &int_zero},
	{ "device_add", REFCOUNT_INC, 0, "$->kobj.kset->kobj.kref.refcount.refs.counter", &int_zero, &int_zero},

	{ "refcount_set", REFCOUNT_INIT, 0, "$->refs.counter" },

	{ "refcount_inc", REFCOUNT_INC, 0, "$->refs.counter" },
	{ "refcount_dec", REFCOUNT_DEC, 0, "$->refs.counter" },
	{ "refcount_dec_and_test", REFCOUNT_DEC, 0, "$->refs.counter" },
	{ "refcount_add", REFCOUNT_INC, 1, "$->refs.counter" },
	{ "refcount_sub_and_test", REFCOUNT_DEC, 1, "$->refs.counter" },

	{ "pm_runtime_get_sync", REFCOUNT_INC, 0, "$->power.usage_count.counter" },

	{ "refcount_inc_not_zero", REFCOUNT_INC, 0, "$->refs.counter", &int_one, &int_one},
	{ "refcount_add_not_zero", REFCOUNT_INC, 1, "$->refs.counter", &int_one, &int_one},

	{ "atomic_dec_if_positive", REFCOUNT_DEC, 0, "$->counter", &int_zero, &int_max},
	{ "atomic64_dec_if_positive", REFCOUNT_DEC, 0, "$->counter", &int_zero, &int_max},

	{ "of_node_get", REFCOUNT_INC, 0, "$->kobj.kref.refcount.refs.counter" },
	{ "of_node_put", REFCOUNT_DEC, 0, "$->kobj.kref.refcount.refs.counter" },
	{ "of_get_parent", REFCOUNT_INC, -1, "$->kobj.kref.refcount.refs.counter" },
	{ "of_clk_del_provider", REFCOUNT_DEC, 0, "$->kobj.kref.refcount.refs.counter" },

	{ "kfree_skb", REFCOUNT_DEC, 0, "$->users.refs.counter" },

	{ "pci_get_dev_by_id", REFCOUNT_INC, -1, "$->dev.kobj.kref.refcount.refs.counter" },
	{ "pci_get_dev_by_id", REFCOUNT_DEC,  1, "$->dev.kobj.kref.refcount.refs.counter" },

	{ "fget", REFCOUNT_INC, -1, "$->f_count.counter", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "sockfd_lookup", REFCOUNT_INC, -1, "$->file->f_count.counter", &valid_ptr_min_sval, &valid_ptr_max_sval },
	{ "skb_get", REFCOUNT_INC, 0, "$->users.refs.counter", },
//	{ "fsnotify_put_mark", REFCOUNT_DEC, 0, "$->refcnt.refs.counter" },

	{ "dma_buf_put", REFCOUNT_DEC, 0, "$->file->f_count.counter" },
};

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (parent_is_null_var_sym(sm->name, sm->sym))
		return &gone;
	return &undefined;
}

static struct smatch_state *merge_states(struct smatch_state *s1, struct smatch_state *s2)
{
	if (s1 == &gone)
		return s2;
	if (s2 == &gone)
		return s1;
	return &merged;
}

static struct name_sym_fn_list *init_hooks, *inc_hooks, *dec_hooks;

void add_refcount_init_hook(name_sym_hook *hook)
{
	add_ptr_list(&init_hooks, hook);
}

void add_refcount_inc_hook(name_sym_hook *hook)
{
	add_ptr_list(&inc_hooks, hook);
}

void add_refcount_dec_hook(name_sym_hook *hook)
{
	add_ptr_list(&dec_hooks, hook);
}

void call_hooks(struct name_sym_fn_list *hooks, struct expression *expr,
		const char *name, struct symbol *sym)
{
	name_sym_hook *hook;

	FOR_EACH_PTR(hooks, hook) {
		hook(expr, name, sym);
	} END_FOR_EACH_PTR(hook);
}

static void do_init(struct expression *expr, const char *name, struct symbol *sym)
{
	struct smatch_state *orig;

	call_hooks(init_hooks, expr, name, sym);

	orig = get_state(my_id, name, sym);
	if (orig) {
		set_state(my_id, name, sym, &ignore);
		return;
	}

	set_state(my_id, name, sym, &init);
}

static void do_inc(struct expression *expr, const char *name, struct symbol *sym)
{
	struct smatch_state *orig;

	call_hooks(inc_hooks, expr, name, sym);

	orig = get_state(my_id, name, sym);
	if (orig) {
		set_state(my_id, name, sym, &ignore);
		return;
	}

	set_state(my_id, name, sym, &inc);
}

static void do_dec(struct expression *expr, const char *name, struct symbol *sym)
{
	struct smatch_state *orig;

	call_hooks(dec_hooks, expr, name, sym);

	orig = get_state(my_id, name, sym);
	if (orig) {
		set_state(my_id, name, sym, &ignore);
		return;
	}

	set_state(my_id, name, sym, &dec);
}

static bool is_refcount_primitive(struct expression *expr)
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

static void match_atomic_add(struct expression *expr, const char *name, struct symbol *sym, void *_unused)
{
	struct expression *amount;
	sval_t sval;

	amount = get_argument_from_call_expr(expr->args, 0);
	if (!get_implied_value(amount, &sval)) {
		do_inc(expr, name, sym);
		return;
	}

	if (sval.value == 0) {
		set_state(my_id, name, sym, &ignore);
		return;
	}

	if (sval_is_positive(sval))
		do_inc(expr, name, sym);
	else
		do_dec(expr, name, sym);
}

static void refcount_init(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	if (!data && is_refcount_primitive(expr))
		return;
	do_init(expr, name, sym);
}

static void refcount_inc(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	if (!data && is_refcount_primitive(expr))
		return;
	do_inc(expr, name, sym);
}

static void refcount_dec(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	if (!data && is_refcount_primitive(expr))
		return;
	do_dec(expr, name, sym);
}

static void match_return_info(int return_id, char *return_ranges,
				       struct expression *returned_expr,
				       int param,
				       const char *printed_name,
				       struct sm_state *sm)
{
	struct smatch_state *state;
	int type;

	if (param == -1 && return_ranges && strcmp(return_ranges, "0") == 0)
		return;

	state = sm->state;
	if (state == &init)
		type = REFCOUNT_INIT;
	else if (state == &inc)
		type = REFCOUNT_INC;
	else if (state == &dec)
		type = REFCOUNT_DEC;
	else
		return;

	sql_insert_return_states(return_id, return_ranges, type, param, printed_name, "");
}

static void match_asm(struct statement *stmt)
{
	struct expression *expr;
	struct asm_operand *op;
	struct symbol *sym;
	bool inc = false;
	char *macro;
	char *name;

	macro = get_macro_name(stmt->pos);
	if (!macro)
		return;

	if (strcmp(macro, "this_cpu_inc") == 0)
		inc = true;
	else if (strcmp(macro, "this_cpu_dec") != 0)
		return;

	op = first_ptr_list((struct ptr_list *)stmt->asm_outputs);
	if (!op)
		return;

	expr = strip_expr(op->expr);
	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		return;

	if (inc)
		do_inc(expr, name, sym);
	else
		do_dec(expr, name, sym);
}

void register_refcount_info(int id)
{
	struct ref_func_info *info;
	param_key_hook *cb;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];

		if (info->call_back)
			cb = info->call_back;
		else if (info->type == REFCOUNT_INIT)
			cb = refcount_init;
		else if (info->type == REFCOUNT_INC)
			cb = refcount_inc;
		else
			cb = refcount_dec;

		if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start, *info->implies_end,
					cb, info->param, info->key, info);
		} else {
			add_function_param_key_hook_late(info->name, cb,
					info->param, info->key, info);
		}
	}

	add_merge_hook(my_id, &merge_states);
	add_return_info_callback(my_id, &match_return_info);
	add_hook(match_asm, ASM_HOOK);

	select_return_param_key(REFCOUNT_INC, &refcount_inc);
	select_return_param_key(REFCOUNT_DEC, &refcount_dec);

	add_unmatched_state_hook(my_id, &unmatched_state);
}
