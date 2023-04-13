/*
 * Copyright (C) 2010 Dan Carpenter.
 * Copyright (C) 2020 Oracle.
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
 * This is the "strict" version which is more daring and ambitious than
 * the check_free.c file.  The difference is that this looks at split
 * returns and the other only looks at if every path frees a parameter.
 * Also this has a bunch of kernel specific things to do with reference
 * counted memory.
 */

#include <string.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(freed);
STATE(maybe_freed);
STATE(ok);

static void match_kobject_put(struct expression *expr, const char *name, struct symbol *sym, void *data);

struct func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	param_key_hook *call_back;
};

static struct func_info func_table[] = {
	{ "free", PARAM_FREED, 0, "$" },
	{ "kfree", PARAM_FREED, 0, "$" },
	{ "vfree", PARAM_FREED, 0, "$" },
	{ "kzfree", PARAM_FREED, 0, "$" },
	{ "kvfree", PARAM_FREED, 0, "$" },

	{ "kfree_skb", PARAM_FREED, 0, "$" },
	{ "kfree_skbmem", PARAM_FREED, 0, "$" },

	{ "mempool_free", PARAM_FREED, 0, "$" },
	{ "kmem_cache_free", PARAM_FREED, 1, "$" },
	{ "dma_pool_free", PARAM_FREED, 1, "$" },

	{ "memstick_free_host", PARAM_FREED, 0, "$" },
//	{ "spi_unregister_controller", PARAM_FREED, 0, "$" },
	{ "netif_rx_internal", PARAM_FREED, 0, "$" },
	{ "netif_rx", PARAM_FREED, 0, "$" },

	{ "enqueue_to_backlog", PARAM_FREED, 0, "$" },

	{ "brelse", PARAM_FREED, 0, "$" },
	{ "dma_free_coherent", PARAM_FREED, 2, "$" },
	{ "free_netdev", PARAM_FREED, 0, "$" },
	{ "sock_release", PARAM_FREED, 0, "$" },

	{ "qdisc_enqueue", PARAM_FREED, 0, "$" },

	{ "kobject_put", PARAM_FREED, 0, "$", NULL, NULL, &match_kobject_put },
	{ "kref_put", PARAM_FREED, 0, "$", NULL, NULL, &match_kobject_put },
	{ "put_device", PARAM_FREED, 0, "$", NULL, NULL, &match_kobject_put },
};

static struct name_sym_fn_list *free_hooks;

void add_free_hook(name_sym_hook *hook)
{
	add_ptr_list(&free_hooks, hook);
}

static void call_free_call_backs_name_sym(struct expression *expr, const char *name, struct symbol *sym)
{
	name_sym_hook *hook;

	if (!expr)
		expr = gen_expression_from_name_sym(name, sym);

	FOR_EACH_PTR(free_hooks, hook) {
		hook(expr, name, sym);
	} END_FOR_EACH_PTR(hook);
}

static void ok_to_use(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	if (is_impossible_path())
		set_state(my_id, cur->name, cur->sym, &ok);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;
	sval_t sval;

	if (sm->state != &freed && sm->state != &maybe_freed)
		return &undefined;

	if (get_param_num_from_sym(sm->sym) < 0)
		return &undefined;

	/*
	 * If the parent is non-there count it as freed.  This is
	 * a hack for tracking return states.
	 */
	if (parent_is_null_var_sym(sm->name, sm->sym))
		return sm->state;

	state = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (!state)
		return &undefined;
	if (!estate_get_single_value(state, &sval) || sval.value != 0)
		return &undefined;
	/* It makes it easier to consider NULL pointers as freed.  */
	return sm->state;
}

struct smatch_state *merge_frees(struct smatch_state *s1, struct smatch_state *s2)
{
	if (s1 == &freed && s2 == &maybe_freed)
		return &maybe_freed;
	if (s1 == &maybe_freed && s2 == &freed)
		return &maybe_freed;
	return &merged;
}

static int is_freed(struct expression *expr)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (sm && slist_has_state(sm->possible, &freed))
		return 1;
	return 0;
}

bool is_freed_var_sym(const char *name, struct symbol *sym)
{
	struct smatch_state *state;

	state = get_state(my_id, name, sym);
	if (state == &freed || state == &maybe_freed)
		return true;

	return false;
}

static bool expr_is_condition(struct expression *expr)
{
	struct statement *stmt;

	stmt = expr_get_parent_stmt(expr);
	if (!stmt)
		return false;
	if (stmt->type == STMT_IF || stmt->type == STMT_ITERATOR)
		return true;
	return false;
}

bool is_part_of_condition(struct expression *expr)
{
	struct expression *parent;

	if (expr_is_condition(expr))
		return true;

	parent = expr_get_parent_expr(expr);
	if (!parent)
		return false;
	if (parent->type == EXPR_LOGICAL || parent->type == EXPR_COMPARE)
		return true;
	if (parent->type == EXPR_SELECT || parent->type == EXPR_CONDITIONAL)
		return true;
	if (parent->type == EXPR_PREOP && parent->op == '!')
		return true;

	return false;
}

static bool is_percent_p(struct expression *str_expr, int idx)
{
	char *p;
	int cnt = 0;

	p = str_expr->string->data;
	while (p[0]) {
		if (p[0] == '%' && p[1] == '%') {
			p += 2;
			continue;
		}
		/* If we have print("%.*s %p", prec, str, p); then it takes 2 params */
		if ((p[0] == '%' && p[1] == '*') ||
		    (p[0] == '%' && p[1] == '.' && p[2] == '*'))
			cnt++;
		if (p[0] == '%') {
			cnt++;
			if (idx == cnt && p[1] == 'p')
				return true;
		}
		p++;
	}
	return false;
}

bool is_percent_p_print(struct expression *expr)
{
	struct expression *parent, *arg;
	int expr_idx, string_idx;

	parent = expr_get_parent_expr(expr);
	if (!parent || parent->type != EXPR_CALL)
		return false;

	expr_idx = -1;
	FOR_EACH_PTR(parent->args, arg) {
		expr_idx++;
		if (arg == expr)
			goto found;
	} END_FOR_EACH_PTR(arg);

	return false;
found:

	string_idx = -1;
	FOR_EACH_PTR(parent->args, arg) {
		string_idx++;
		if (arg->type != EXPR_STRING)
			continue;
		if (is_percent_p(arg, expr_idx - string_idx))
			return true;
	} END_FOR_EACH_PTR(arg);

	return false;
}

static void match_symbol(struct expression *expr)
{
	struct expression *parent;
	char *name;

	if (is_impossible_path())
		return;
	if (__in_fake_parameter_assign)
		return;

	if (is_part_of_condition(expr))
		return;

	/* This ignores stuff like "get_new_ptr(&foo);" */
	parent = expr_get_parent_expr(expr);
	while (parent && parent->type == EXPR_PREOP && parent->op == '(')
		parent = expr_get_parent_expr(parent);
	if (parent && parent->type == EXPR_PREOP && parent->op == '&')
		return;

	if (!is_freed(expr))
		return;

	if (is_percent_p_print(expr))
		return;

	name = expr_to_var(expr);
	sm_warning("'%s' was already freed.", name);
	free_string(name);
}

static void match_dereferences(struct expression *expr)
{
	char *name;

	if (expr->type != EXPR_PREOP)
		return;

	if (is_impossible_path())
		return;
	if (__in_fake_parameter_assign)
		return;

	expr = strip_expr(expr->unop);
	if (!is_freed(expr))
		return;
	name = expr_to_var(expr);
	sm_error("dereferencing freed memory '%s'", name);
	set_state_expr(my_id, expr, &ok);
	free_string(name);
}

static int ignored_params[16];

static void set_ignored_params(struct expression *call)
{
	struct expression *arg;
	const char *p;
	int i;

	memset(&ignored_params, 0, sizeof(ignored_params));

	i = -1;
	FOR_EACH_PTR(call->args, arg) {
		i++;
		if (arg->type != EXPR_STRING)
			continue;
		goto found;
	} END_FOR_EACH_PTR(arg);

	return;

found:
	i++;
	p = arg->string->data;
	while ((p = strchr(p, '%'))) {
		if (i >= ARRAY_SIZE(ignored_params))
			return;
		p++;
		if (*p == '%') {
			p++;
			continue;
		}
		if (*p == '.')
			p++;
		if (*p == '*')
			i++;
		if (*p == 'p')
			ignored_params[i] = 1;
		i++;
	}
}

static int is_free_func(struct expression *fn)
{
	char *name;
	int ret = 0;

	name = expr_to_str(fn);
	if (!name)
		return 0;
	if (strstr(name, "free"))
		ret = 1;
	free_string(name);

	return ret;
}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	char *name;
	int i;

	if (is_impossible_path())
		return;

	set_ignored_params(expr);

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		if (!is_pointer(arg))
			continue;
		if (!is_freed(arg))
			continue;
		if (ignored_params[i])
			continue;
		if (is_percent_p_print(arg))
			continue;

		name = expr_to_var(arg);
		if (is_free_func(expr->fn))
			sm_error("double free of '%s'", name);
		else
			sm_warning("passing freed memory '%s'", name);
		set_state_expr(my_id, arg, &ok);
		free_string(name);
	} END_FOR_EACH_PTR(arg);
}

static void match_return(struct expression *expr)
{
	char *name;

	if (is_impossible_path())
		return;

	if (!expr)
		return;
	if (!is_freed(expr))
		return;

	name = expr_to_var(expr);
	sm_warning("returning freed memory '%s'", name);
	set_state_expr(my_id, expr, &ok);
	free_string(name);
}

static int counter_was_inced_name_sym(const char *name, struct symbol *sym, const char *counter_str)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "%s%s", name, counter_str);
	return was_inced(buf, sym);
}

static int counter_was_inced(struct expression *expr, const char *counter_str)
{
	char *name;
	struct symbol *sym;
	int ret = 0;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	ret = counter_was_inced_name_sym(name, sym, counter_str);
free:
	free_string(name);
	return ret;
}

static bool is_ptr_to(struct expression *expr, const char *type)
{
	struct symbol *sym;

	sym = get_type(expr);
	if (!is_ptr_type(sym))
		return false;
	sym = get_real_base_type(sym);
	if (sym && sym->ident && strcmp(sym->ident->name, type) == 0)
		return true;
	return false;
}

static void match_free(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct expression *arg;

	if (is_impossible_path())
		return;

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;
	if (is_ptr_to(arg, "sk_buff") &&
	    counter_was_inced(arg, "->users.refs.counter"))
		return;
	if (is_ptr_to(arg, "buffer_head") &&
	    counter_was_inced(arg, "->b_count.counter"))
		return;
	if (is_freed(arg))
		sm_error("double free of '%s'", name);

	track_freed_param(arg, &freed);
	call_free_call_backs_name_sym(expr, name, sym);
	set_state_expr(my_id, arg, &freed);
}


static void match_kobject_put(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct expression *arg;

	arg = gen_expression_from_name_sym(name, sym);
	if (!arg)
		return;
	/* kobject_put(&cdev->kobj); */
	if (arg->type != EXPR_PREOP || arg->op != '&')
		return;
	arg = strip_expr(arg->unop);
	if (arg->type != EXPR_DEREF)
		return;
	arg = strip_expr(arg->deref);
	if (arg->type != EXPR_PREOP || arg->op != '*')
		return;
	arg = strip_expr(arg->unop);
	track_freed_param(arg, &maybe_freed);
	set_state_expr(my_id, arg, &maybe_freed);
}

struct string_list *handled;
static bool is_handled_func(struct expression *fn)
{
	if (!fn || fn->type != EXPR_SYMBOL || !fn->symbol->ident)
		return false;

	return list_has_string(handled, fn->symbol->ident->name);
}

static void set_param_helper(struct expression *expr, int param,
			     char *key, char *value,
			     struct smatch_state *state)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;
	struct sm_state *sm;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	if (is_handled_func(expr->fn))
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;
	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	/* skbs are not free if we called skb_get(). */
	if (counter_was_inced_name_sym(name, sym, "->users.refs.counter"))
		goto free;

	if (state == &freed && !is_impossible_path()) {
		sm = get_sm_state(my_id, name, sym);
		if (sm && slist_has_state(sm->possible, &freed)) {
			sm_warning("'%s' double freed", name);
			set_state(my_id, name, sym, &ok);  /* fixme: doesn't silence anything.  I know */
		}
	}

	track_freed_param_var_sym(name, sym, state);
	if (state == &freed)
		call_free_call_backs_name_sym(NULL, name, sym);
	set_state(my_id, name, sym, state);
free:
	free_string(name);
}

static void set_param_freed(struct expression *expr, int param, char *key, char *value)
{
	set_param_helper(expr, param, key, value, &freed);
}

static void set_param_maybe_freed(struct expression *expr, int param, char *key, char *value)
{
	set_param_helper(expr, param, key, value, &maybe_freed);
}

int parent_is_free_var_sym_strict(const char *name, struct symbol *sym)
{
	char buf[256];
	char *start;
	char *end;
	char orig;
	struct smatch_state *state;

	strncpy(buf, name, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	start = &buf[0];
	while ((*start == '&'))
		start++;

	end = start;
	while ((end = strrchr(end, '-'))) {
		orig = *end;
		*end = '\0';

		state = __get_state(my_id, start, sym);
		if (state == &freed)
			return 1;
		*end = orig;
		end++;
	}
	return 0;
}

int parent_is_free_strict(struct expression *expr)
{
	struct symbol *sym;
	char *var;
	int ret = 0;

	expr = strip_expr(expr);
	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto free;
	ret = parent_is_free_var_sym_strict(var, sym);
free:
	free_string(var);
	return ret;
}

static void match_untracked(struct expression *call, int param)
{
	struct state_list *slist = NULL;
	struct expression *arg;
	struct sm_state *sm;
	char *name;
	char buf[64];
	int len;

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		return;

	name = expr_to_var(arg);
	if (!name)
		return;
	snprintf(buf, sizeof(buf), "%s->", name);
	free_string(name);
	len = strlen(buf);

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (strncmp(sm->name, buf, len) == 0)
			add_ptr_list(&slist, sm);
	} END_FOR_EACH_SM(sm);

	FOR_EACH_PTR(slist, sm) {
		set_state(sm->owner, sm->name, sm->sym, &ok);
	} END_FOR_EACH_PTR(sm);

	free_slist(&slist);
}

void check_free_strict(int id)
{
	struct func_info *info;
	param_key_hook *cb;
	int i;

	my_id = id;

	if (option_project != PROJ_KERNEL)
		return;

	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		info = &func_table[i];

		insert_string(&handled, info->name);

		if (info->call_back)
			cb = info->call_back;
		else
			cb = &match_free;

		if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start, *info->implies_end,
					cb, info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name, cb,
					info->param, info->key, info);
		}
	}

	if (option_spammy)
		add_hook(&match_symbol, SYM_HOOK);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&match_return, RETURN_HOOK);

	add_modification_hook_late(my_id, &ok_to_use);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_frees);

	select_return_states_hook(PARAM_FREED, &set_param_freed);
	select_return_states_hook(MAYBE_FREED, &set_param_maybe_freed);
	add_untracked_param_hook(&match_untracked);
}
