/*
 * Copyright (C) 2016 Oracle.
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

#include <ctype.h>

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;
static int test_id;

STATE(inc);
STATE(start_state);
STATE(dec);

STATE(zero_path);

struct ref_func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	func_hook *call_back;
};

static void match_atomic_add(const char *fn, struct expression *expr, void *_unused);

static struct ref_func_info func_table[] = {
	{ "atomic_inc", ATOMIC_INC, 0, "$->counter" },
	{ "atomic_long_inc", ATOMIC_INC, 0, "$->counter" },
	{ "atomic64_inc", ATOMIC_INC, 0, "$->counter" },

	{ "atomic_inc_return", ATOMIC_INC, 0, "$->counter" },
	{ "atomic_long_inc_return", ATOMIC_INC, 0, "$->counter" },
	{ "atomic64_return", ATOMIC_INC, 0, "$->counter" },

	{ "atomic_add_return", ATOMIC_INC, 1, "$->counter", NULL, NULL, match_atomic_add },
	{ "atomic_long_add_return", ATOMIC_INC, 1, "$->counter", NULL, NULL, match_atomic_add },
	{ "atomic64_add_return", ATOMIC_INC, 1, "$->counter", NULL, NULL, match_atomic_add },

	{ "atomic_dec", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic_long_dec", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic64_dec", ATOMIC_DEC, 0, "$->counter" },

	{ "atomic_dec_return", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic_long_dec_return", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic64_dec_return", ATOMIC_DEC, 0, "$->counter" },

	{ "atomic_dec_and_test", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic_long_dec_and_test", ATOMIC_DEC, 0, "$->counter" },
	{ "atomic64_dec_and_test", ATOMIC_DEC, 0, "$->counter" },

	{ "_atomic_dec_and_lock", ATOMIC_DEC, 0, "$->counter" },

	{ "atomic_sub", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic_long_sub", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic64_sub", ATOMIC_DEC, 1, "$->counter" },

	{ "atomic_sub_return", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic_long_sub_return", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic64_sub_return", ATOMIC_DEC, 1, "$->counter" },

	{ "atomic_sub_and_test", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic_long_sub_and_test", ATOMIC_DEC, 1, "$->counter" },
	{ "atomic64_sub_and_test", ATOMIC_DEC, 1, "$->counter" },

	{ "refcount_inc", ATOMIC_INC, 0, "$->refs.counter" },
	{ "refcount_dec", ATOMIC_DEC, 0, "$->refs.counter" },
	{ "refcount_dec_and_test", ATOMIC_DEC, 0, "$->refs.counter" },
	{ "refcount_add", ATOMIC_INC, 1, "$->refs.counter" },
	{ "refcount_sub_and_test", ATOMIC_DEC, 1, "$->refs.counter" },

	{ "pm_runtime_get_sync", ATOMIC_INC, 0, "$->power.usage_count.counter" },
	{ "of_clk_del_provider", ATOMIC_DEC, 0, "$->kobj.kref.refcount.refs.counter" },

	{ "refcount_inc_not_zero", ATOMIC_INC, 0, "$->refs.counter", &int_one, &int_one},
	{ "refcount_add_not_zero", ATOMIC_INC, 1, "$->refs.counter", &int_one, &int_one},

	{ "atomic_dec_if_positive", ATOMIC_DEC, 0, "$->counter", &int_zero, &int_max},
	{ "atomic64_dec_if_positive", ATOMIC_DEC, 0, "$->counter", &int_zero, &int_max},

	{ "of_node_get", ATOMIC_INC, 0, "$->kobj.kref.refcount.refs.counter" },
	{ "of_node_put", ATOMIC_DEC, 0, "$->kobj.kref.refcount.refs.counter" },
	{ "of_get_parent", ATOMIC_INC, -1, "$->kobj.kref.refcount.refs.counter" },
	{ "of_clk_del_provider", ATOMIC_DEC, 0, "$->kobj.kref.refcount.refs.counter" },

	{ "kfree_skb", ATOMIC_DEC, 0, "$->users.refs.counter" },
};

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	/*
	 * We default to decremented.  For example, say we have:
	 * 	if (p)
	 *		atomic_dec(p);
	 *      <- p is decreemented.
	 *
	 */
	if ((sm->state == &dec) &&
	    parent_is_gone_var_sym(sm->name, sm->sym))
		return sm->state;
	return &start_state;
}

static struct stree *start_states;
static void set_start_state(const char *name, struct symbol *sym, struct smatch_state *start)
{
	struct smatch_state *orig;

	orig = get_state_stree(start_states, my_id, name, sym);
	if (!orig)
		set_state_stree(&start_states, my_id, name, sym, start);
	else if (orig != start)
		set_state_stree(&start_states, my_id, name, sym, &undefined);
}

static struct sm_state *get_best_match(const char *key)
{
	struct sm_state *sm;
	struct sm_state *match;
	int cnt = 0;
	int start_pos, state_len, key_len, chunks, i;

	if (strncmp(key, "$->", 3) == 0)
		key += 3;

	key_len = strlen(key);
	chunks = 0;
	for (i = key_len - 1; i > 0; i--) {
		if (key[i] == '>' || key[i] == '.')
			chunks++;
		if (chunks == 3) {
			key += (i + 1);
			key_len = strlen(key);
			break;
		}
	}

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		state_len = strlen(sm->name);
		if (state_len < key_len)
			continue;
		start_pos = state_len - key_len;
		if ((start_pos == 0 || !isalnum(sm->name[start_pos - 1])) &&
		    strcmp(sm->name + start_pos, key) == 0) {
			cnt++;
			match = sm;
		}
	} END_FOR_EACH_SM(sm);

	if (cnt == 1)
		return match;
	return NULL;
}

static void handle_test_functions(struct expression *expr)
{
	struct expression *tmp;
	struct statement *stmt;
	int count = 0;

	if (expr->type != EXPR_CALL ||
	    expr->fn->type != EXPR_SYMBOL ||
	    !expr->fn->symbol_name)
		return;
	if (!strstr(expr->fn->symbol_name->name, "test"))
		return;

	while ((tmp = expr_get_parent_expr(expr))) {
		expr = tmp;
		if (count++ > 5)
			break;
	}

	stmt = expr_get_parent_stmt(expr);
	if (!stmt || stmt->type != STMT_IF)
		return;

	set_true_false_states(test_id, "dec_path", NULL, &zero_path, NULL);
}

static void db_inc_dec(struct expression *expr, int param, const char *key, int inc_dec)
{
	struct sm_state *start_sm;
	struct expression *call, *arg;
	char *name;
	struct symbol *sym;
	bool free_at_end = true;

	call = expr;
	while (call && call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);

	if (!call || call->type != EXPR_CALL)
		return;

	handle_test_functions(call);

	if (param == -1 &&
	    expr->type == EXPR_ASSIGNMENT &&
	    expr->op == '=') {
		name = get_variable_from_key(expr->left, key, &sym);
		if (!name || !sym)
			goto free;
	} else if (param >= 0) {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return;

		name = get_variable_from_key(arg, key, &sym);
		if (!name || !sym)
			goto free;
	} else {
		name = alloc_string(key);
		sym = NULL;
	}

	start_sm = get_sm_state(my_id, name, sym);
	if (!start_sm && !sym && inc_dec == ATOMIC_DEC) {
		start_sm = get_best_match(key);
		if (start_sm) {
			free_string(name);
			free_at_end = false;
			name = (char *)start_sm->name;
			sym = start_sm->sym;
		}
	}

	if (inc_dec == ATOMIC_INC) {
		if (!start_sm)
			set_start_state(name, sym, &dec);
//		set_refcount_inc(name, sym);
		set_state(my_id, name, sym, &inc);
	} else {
//		set_refcount_dec(name, sym);
		if (!start_sm)
			set_start_state(name, sym, &inc);

		if (start_sm && start_sm->state == &inc)
			set_state(my_id, name, sym, &start_state);
		else
			set_state(my_id, name, sym, &dec);
	}

free:
	if (free_at_end)
		free_string(name);
}

static bool is_inc_dec_primitive(struct expression *expr)
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

static void db_inc(struct expression *expr, int param, char *key, char *value)
{
	if (is_inc_dec_primitive(expr))
		return;
	db_inc_dec(expr, param, key, ATOMIC_INC);
}

static void db_dec(struct expression *expr, int param, char *key, char *value)
{
	if (is_inc_dec_primitive(expr))
		return;
	db_inc_dec(expr, param, key, ATOMIC_DEC);
}

static void match_atomic_add(const char *fn, struct expression *expr, void *_unused)
{
	struct expression *amount;
	sval_t sval;

	amount = get_argument_from_call_expr(expr->args, 0);
	if (get_implied_value(amount, &sval) && sval_is_negative(sval)) {
		db_inc_dec(expr, 1, "$->counter", ATOMIC_DEC);
		return;
	}

	db_inc_dec(expr, 1, "$->counter", ATOMIC_INC);
}

static void refcount_function(const char *fn, struct expression *expr, void *data)
{
	struct ref_func_info *info = data;

	db_inc_dec(expr, info->param, info->key, info->type);
}

static void refcount_implied(const char *fn, struct expression *call_expr,
			     struct expression *assign_expr, void *data)
{
	struct ref_func_info *info = data;

	db_inc_dec(assign_expr ?: call_expr, info->param, info->key, info->type);
}

static bool is_maybe_dec(struct sm_state *sm)
{
	if (sm->state == &dec)
		return true;
	if (slist_has_state(sm->possible, &dec) &&
	    !slist_has_state(sm->possible, &inc))
		return true;
	return false;
}

static void match_return_info(int return_id, char *return_ranges, struct expression *expr)
{
	struct sm_state *sm;
	const char *param_name;
	int param;

	if (is_impossible_path())
		return;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state != &inc && !is_maybe_dec(sm))
			continue;
		if (sm->state == get_state_stree(start_states, my_id, sm->name, sm->sym))
			continue;
		if (parent_is_gone_var_sym(sm->name, sm->sym))
			continue;
		param = get_param_num_from_sym(sm->sym);
		if (param < 0)
			continue;
		param_name = get_param_name(sm);
		if (!param_name)
			continue;
		sql_insert_return_states(return_id, return_ranges,
					 (sm->state == &inc) ? ATOMIC_INC : ATOMIC_DEC,
					 param, param_name, "");
	} END_FOR_EACH_SM(sm);
}

enum {
	EMPTY, NEGATIVE, ZERO, POSITIVE, NUM_BUCKETS
};

static int success_fail_positive(struct range_list *rl)
{
	if (!rl)
		return EMPTY;

	if (!is_whole_rl(rl) && sval_is_negative(rl_min(rl)))
		return NEGATIVE;

	if (rl_min(rl).value == 0)
		return ZERO;

	return POSITIVE;
}

static void check_counter(const char *name, struct symbol *sym)
{
	struct range_list *inc_lines = NULL;
	struct range_list *dec_lines = NULL;
	int inc_buckets[NUM_BUCKETS] = {};
	int dec_buckets[NUM_BUCKETS] = {};
	struct stree *stree, *orig_stree;
	struct smatch_state *state;
	struct sm_state *return_sm;
	struct sm_state *sm;
	sval_t line = sval_type_val(&int_ctype, 0);
	int bucket;

	/* don't warn about stuff we can't identify */
	if (!sym)
		return;

	/* static variable are probably just counters */
	if (sym->ctype.modifiers & MOD_STATIC &&
	    !(sym->ctype.modifiers & MOD_TOPLEVEL))
		return;

	if (strstr(name, "error") ||
	    strstr(name, "drop") ||
	    strstr(name, "xmt_ls_err") ||
	    strstr(name, "->stats->") ||
	    strstr(name, "->stats."))
		return;

	if (strstr(name, "power.usage_count"))
		return;

	FOR_EACH_PTR(get_all_return_strees(), stree) {
		orig_stree = __swap_cur_stree(stree);

		if (is_impossible_path())
			goto swap_stree;

		return_sm = get_sm_state(RETURN_ID, "return_ranges", NULL);
		if (!return_sm)
			goto swap_stree;
		line.value = return_sm->line;

		if (get_state_stree(start_states, my_id, name, sym) == &inc)
			goto swap_stree;

		if (parent_is_gone_var_sym(name, sym))
			goto swap_stree;

		sm = get_sm_state(my_id, name, sym);
		if (sm)
			state = sm->state;
		else
			state = &start_state;

		if (state != &inc &&
		    state != &dec &&
		    state != &start_state)
			goto swap_stree;

		bucket = success_fail_positive(estate_rl(return_sm->state));

		if (state == &inc) {
			add_range(&inc_lines, line, line);
			inc_buckets[bucket] = true;
		}
		if (state == &dec || state == &start_state) {
			add_range(&dec_lines, line, line);
			dec_buckets[bucket] = true;
		}
swap_stree:
		__swap_cur_stree(orig_stree);
	} END_FOR_EACH_PTR(stree);

	if (inc_buckets[NEGATIVE] &&
	    inc_buckets[ZERO]) {
		// sm_warning("XXX '%s' not decremented on lines: %s.", name, show_rl(inc_lines));
	}

}

static void match_check_missed(struct symbol *sym)
{
	struct sm_state *sm;

	FOR_EACH_MY_SM(my_id, get_all_return_states(), sm) {
		check_counter(sm->name, sm->sym);
	} END_FOR_EACH_SM(sm);
}

int on_atomic_dec_path(void)
{
	return get_state(test_id, "dec_path", NULL) == &zero_path;
}

int was_inced(const char *name, struct symbol *sym)
{
	return get_state(my_id, name, sym) == &inc;
}

static void match_after_func(struct symbol *sym)
{
	free_stree(&start_states);
}

void check_atomic_inc_dec(int id)
{
	struct ref_func_info *info;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	add_unmatched_state_hook(my_id, &unmatched_state);

	add_split_return_callback(match_return_info);
	select_return_states_hook(ATOMIC_INC, &db_inc);
	select_return_states_hook(ATOMIC_DEC, &db_dec);

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];

		if (info->call_back) {
			add_function_hook(info->name, info->call_back, info);
		} else if (info->implies_start) {
			return_implies_state_sval(info->name,
					*info->implies_start, *info->implies_end,
					&refcount_implied, info);
		} else {
			add_function_hook(info->name, &refcount_function, info);
		}
	}

	add_hook(&match_check_missed, END_FUNC_HOOK);

	add_hook(&match_after_func, AFTER_FUNC_HOOK);
	add_function_data((unsigned long *)&start_states);
}

void check_atomic_test(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	test_id = id;
}
