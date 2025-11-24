/*
 * Copyright (C) 2010 Dan Carpenter.
 * Copyright (C) 2020 Oracle.
 * Copyright 2025 Linaro Ltd.
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

#include <string.h>
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

#define IGNORE -1

static void match_kobject_put(struct expression *expr, const char *name, struct symbol *sym, void *data);
static void match___skb_pad(struct expression *expr, const char *name, struct symbol *sym, void *data);

struct func_info {
	const char *name;
	int type;
	int param;
	const char *key;
	const sval_t *implies_start, *implies_end;
	param_key_hook *call_back;
};

static struct func_info *free_table;

static struct func_info default_func_table[] = {
	{ "free", PARAM_FREED, 0, "$" },
	{ /* sentinel */ }
};

static struct func_info illumos_func_table[] = {
	{ "kmem_free", PARAM_FREED, 0, "$" },
	{ /* sentinel */ }
};

static struct func_info func_table[] = {
	{ "consume_skb", PARAM_FREED, 0, "$" },
	{ "brelse", PARAM_FREED, 0, "$" },
	{ "dma_fence_put", PARAM_FREED, 0, "$" },
	{ "dma_free_coherent", PARAM_FREED, 2, "$" },
	{ "dma_pool_free", PARAM_FREED, 1, "$" },
	{ "enqueue_to_backlog", PARAM_FREED, 0, "$" },
	{ "free_netdev", PARAM_FREED, 0, "$" },
	{ "free", PARAM_FREED, 0, "$" },
	{ "kfree", PARAM_FREED, 0, "$" },
	{ "kfree_skbmem", PARAM_FREED, 0, "$" },
	{ "kfree_skb", PARAM_FREED, 0, "$" },
	{ "kmem_cache_free", PARAM_FREED, 1, "$" },
	{ "kobject_put", PARAM_FREED, 0, "$", NULL, NULL, &match_kobject_put },
	{ "kvfree_call_rcu", PARAM_FREED, 1, "$" },
	{ "kvfree", PARAM_FREED, 0, "$" },
	{ "kzfree", PARAM_FREED, 0, "$" },
	{ "mempool_free", PARAM_FREED, 0, "$" },
	{ "memstick_free_host", PARAM_FREED, 0, "$" },
	{ "netif_rx_internal", PARAM_FREED, 0, "$" },
	{ "netif_rx", PARAM_FREED, 0, "$" },
	{ "put_device", PARAM_FREED, 0, "$", NULL, NULL, &match_kobject_put },
	{ "qdisc_enqueue", PARAM_FREED, 0, "$" },
	{ "__skb_pad", PARAM_FREED, 0, "$", &err_min, &err_max, &match___skb_pad },
	{ "skb_unshare", IGNORE, 0, "$" },
	{ "sock_release", PARAM_FREED, 0, "$" },
//	{ "spi_unregister_controller", PARAM_FREED, 0, "$" },
	{ "vfree", PARAM_FREED, 0, "$" },
	{ /* sentinel */ }
};

static struct name_sym_fn_list *free_hooks, *maybe_free_hooks;

void add_free_hook(name_sym_hook *hook)
{
	add_ptr_list(&free_hooks, hook);
}

void add_maybe_free_hook(name_sym_hook *hook)
{
	add_ptr_list(&maybe_free_hooks, hook);
}

static void call_free_call_backs_name_sym(int type, struct expression *expr, const char *name, struct symbol *sym)
{
	if (type == PARAM_FREED)
		call_name_sym_fns(free_hooks, expr, name, sym);
	else
		call_name_sym_fns(maybe_free_hooks, expr, name, sym);
}

static void call_free_call_backs_expr(int type, struct expression *expr)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return;
	call_free_call_backs_name_sym(type, expr, name, sym);
	free_string(name);
}

static bool is_free_primitive(struct expression *expr)
{
	const char *name = get_fn_name(expr);
	struct func_info *info;

	if (!name)
		return false;

	for (info = &free_table[0]; info->name; info++) {
		if (strcmp(info->name, name) == 0)
			return true;
	}
	return false;
}

static void set_param_freed_helper(struct expression *expr, const char *name, struct symbol *sym, void *data, int type)
{
	struct func_info *info = data;
	struct expression *call;

	if (info && info->type == IGNORE)
		return;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return;

	if (!info && is_free_primitive(call->fn))
		return;

	if (refcount_was_inced_name_sym(name, sym, "->b_count.counter"))
		return;
	if (refcount_was_inced_name_sym(name, sym, "->kref.refcount.refs.counter"))
		return;
	if (refcount_was_inced_name_sym(name, sym, "->ref.refcount.refs.counter"))
		return;
	if (refcount_was_inced_name_sym(name, sym, "->refcount.refcount.refs.counter"))
		return;
	if (refcount_was_inced_name_sym(name, sym, "->users.refs.counter"))
		return;

	call_free_call_backs_name_sym(type, expr, name, sym);
}

static void set_param_freed(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_param_freed_helper(expr, name, sym, data, PARAM_FREED);
}

static void set_param_maybe_freed(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	set_param_freed_helper(expr, name, sym, data, MAYBE_FREED);
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

	call_free_call_backs_expr(MAYBE_FREED, arg);
}

static void match___skb_pad(struct expression *expr, const char *name, struct symbol *sym, void *data)
{
	struct expression *arg, *skb;
	sval_t sval;
	int type;

	while (expr && expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (!expr || expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, 2);
	if (expr_is_zero(arg))
		return;

	type = MAYBE_FREED;
	if (get_implied_value(arg, &sval) && sval.value != 0)
		type = PARAM_FREED;

	skb = get_argument_from_call_expr(expr->args, 0);
	call_free_call_backs_expr(type, skb);
}

void register_free(int id)
{
	struct func_info *info;
	param_key_hook *cb;

	my_id = id;

	switch (option_project) {
	case PROJ_KERNEL:
		free_table = func_table;
		break;
	case PROJ_ILLUMOS_KERNEL:
		free_table = illumos_func_table;
		break;
	default:
		free_table = default_func_table;
		break;
	}

	for (info = &free_table[0]; info->name; info++) {
		if (info->call_back)
			cb = info->call_back;
		else
			cb = &set_param_freed;

		if (info->implies_start) {
			return_implies_param_key(info->name,
					*info->implies_start, *info->implies_end,
					cb, info->param, info->key, info);
		} else {
			add_function_param_key_hook(info->name, cb,
					info->param, info->key, info);
		}
	}

	select_return_param_key(PARAM_FREED, &set_param_freed);
	select_return_param_key(MAYBE_FREED, &set_param_maybe_freed);
}
