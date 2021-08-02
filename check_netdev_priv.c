/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2021 Pavel Skripkin
 */

/* TODO:
 *
 * Try to find a way how to handle situations like:
 *
 *	struct some_dev *dev = get_dev_from_smth(smth);
 *
 *	free_netdev(dev->netdev);
 *	do_clean_up(dev);
 *
 *
 *	In this case dev is dev->netdev private data (exmpl: ems_usb_disconnect())
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_function_hashtable.h"

static int my_id;
STATE(freed);
STATE(ok);

static void ok_to_use(struct sm_state *sm, struct expression *mod_expr)
{
	if (sm->state != &ok)
		set_state(my_id, sm->name, sm->sym, &ok);
}

static inline char *get_function_name(struct expression *expr)
{
	if (!expr || expr->type != EXPR_CALL)
		return NULL;
	if (expr->fn->type != EXPR_SYMBOL || !expr->fn->symbol_name)
		return NULL;
	return expr->fn->symbol_name->name;
}

static inline int is_netdev_priv(struct expression *call)
{
	char *name;

	if (!call || call->type != EXPR_CALL)
		return 0;

	name = get_function_name(call);
	if (!name)
		return 0;

	return !strcmp("netdev_priv", name);
}

static const char *get_parent_netdev_name(struct expression *expr)
{
	struct expression *call, *arg_expr = NULL;
	struct symbol *sym;

	call = get_assigned_expr(strip_expr(expr));
	if (is_netdev_priv(call)) {
		arg_expr = get_argument_from_call_expr(call->args, 0);
		arg_expr = strip_expr(arg_expr);
	} else {
		return NULL;
	}

	return expr_to_var_sym(arg_expr, &sym);
}

static void match_free_netdev(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *arg;
	const char *name;

	arg = get_argument_from_call_expr(expr->args, PTR_INT(_arg_no));
	if (!arg)
		return;

	name = expr_to_var(arg);
	if (!name)
		return;

	set_state(my_id, name, NULL, &freed);
}

static void match_symbol(struct expression *expr)
{
	const char *parent_netdev, *name;
	struct smatch_state *state;

	name = expr_to_var(expr);
	if (!name)
		return;

	parent_netdev = get_parent_netdev_name(expr);
	if (!parent_netdev)
		return;
	
	state = get_state(my_id, parent_netdev, NULL);
	if (state == &freed)
		sm_error("Using %s after free_{netdev,candev}(%s);\n", name, parent_netdev);
}

void check_uaf_netdev_priv(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;

	add_function_hook("free_netdev", &match_free_netdev, NULL);
	add_function_hook("free_candev", &match_free_netdev, NULL);
	add_modification_hook(my_id, &ok_to_use);
	add_hook(&match_symbol, SYM_HOOK);
}
