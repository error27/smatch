/*
 * sparse/smatch_modification_hooks.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

enum {
	match_none = 0,
	match_exact,
	match_indirect
};

static modification_hook **hooks;
static modification_hook **indirect_hooks;  /* parent struct modified etc */

void add_modification_hook(int owner, modification_hook *call_back)
{
	hooks[owner] = call_back;
}

void add_indirect_modification_hook(int owner, modification_hook *call_back)
{
	indirect_hooks[owner] = call_back;
}

static int matches(char *name, struct symbol *sym, struct sm_state *sm)
{
	int len;

	if (sym != sm->sym)
		return match_none;

	len = strlen(name);
	if (strncmp(sm->name, name, len) == 0) {
		if (sm->name[len] == '\0')
			return match_exact;
		if (sm->name[len] == '-' || sm->name[len] == '.')
			return match_indirect;
	}
	if (sm->name[0] != '*')
		return match_none;
	if (strncmp(sm->name + 1, name, len) == 0) {
		if (sm->name[len + 1] == '\0')
			return match_indirect;
		if (sm->name[len + 1] == '-' || sm->name[len + 1] == '.')
			return match_indirect;
	}
	return match_none;
}

static void call_modification_hooks_name_sym(char *name, struct symbol *sym, struct expression *mod_expr)
{
	struct state_list *slist;
	struct sm_state *sm;
	int match;

	slist = __get_cur_slist();

	FOR_EACH_PTR(slist, sm) {
		if (sm->owner > num_checks)
			continue;
		match = matches(name, sym, sm);

		if (match && hooks[sm->owner])
			(hooks[sm->owner])(sm, mod_expr);

		if (match == match_indirect && indirect_hooks[sm->owner])
			(indirect_hooks[sm->owner])(sm, mod_expr);
	} END_FOR_EACH_PTR(sm);
}

static void call_modification_hooks(struct expression *expr, struct expression *mod_expr)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;
	call_modification_hooks_name_sym(name, sym, mod_expr);
free:
	free_string(name);
}

static void db_param_add(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;
	char *name;
	struct symbol *sym;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	call_modification_hooks_name_sym(name, sym, expr);
free:
	free_string(name);
}

static void match_assign(struct expression *expr)
{
	call_modification_hooks(expr->left, expr);
}

static void unop_expr(struct expression *expr)
{
	if (expr->op != SPECIAL_DECREMENT && expr->op != SPECIAL_INCREMENT)
		return;

	call_modification_hooks(expr->unop, expr);
}

static void match_call(struct expression *expr)
{
	struct expression *arg, *tmp;

	FOR_EACH_PTR(expr->args, arg) {
		tmp = strip_expr(arg);
		if (tmp->type == EXPR_PREOP && tmp->op == '&')
			call_modification_hooks(tmp->unop, expr);
		else if (option_no_db)
			call_modification_hooks(deref_expression(tmp), expr);
	} END_FOR_EACH_PTR(arg);
}

static void asm_expr(struct statement *stmt)
{
	struct expression *expr;
	int state = 0;

	FOR_EACH_PTR(stmt->asm_outputs, expr) {
		switch (state) {
		case 0: /* identifier */
		case 1: /* constraint */
			state++;
			continue;
		case 2: /* expression */
			state = 0;
			call_modification_hooks(expr, NULL);
			continue;
		}
	} END_FOR_EACH_PTR(expr);
}

void register_modification_hooks(int id)
{
	hooks = malloc((num_checks + 1) * sizeof(*hooks));
	memset(hooks, 0, (num_checks + 1) * sizeof(*hooks));
	indirect_hooks = malloc((num_checks + 1) * sizeof(*hooks));
	memset(indirect_hooks, 0, (num_checks + 1) * sizeof(*hooks));

	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&asm_expr, ASM_HOOK);
}

void register_modification_hooks_late(int id)
{
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	select_return_states_hook(ADDED_VALUE, &db_param_add);
}

