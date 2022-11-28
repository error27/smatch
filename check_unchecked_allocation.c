/*
 * Copyright (C) 2010 Dan Carpenter.
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
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

#define __GFP_NOFAIL 0x8000u

STATE(null);

static unsigned long GFP_NOFAIL(void)
{
	static unsigned long saved_flags = -1;
	struct symbol *macro_sym;

	if (saved_flags != -1)
		return saved_flags;

	macro_sym = lookup_macro_symbol("___GFP_NOFAIL");
	if (!macro_sym)
		macro_sym = lookup_macro_symbol("__GFP_NOFAIL");
	if (!macro_sym || !macro_sym->expansion)
		return __GFP_NOFAIL;
	if (token_type(macro_sym->expansion) != TOKEN_NUMBER)
		return __GFP_NOFAIL;

	saved_flags = strtoul(macro_sym->expansion->number, NULL, 0);
	return saved_flags;
}

static void is_ok(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	struct smatch_state *state;

	if (is_impossible_path()) {
		set_state(my_id, cur->name, cur->sym, &undefined);
		return;
	}

	state = get_state(SMATCH_EXTRA, cur->name, cur->sym);
	if (!state || !estate_rl(state))
		return;
	if (!rl_has_sval(estate_rl(state), ptr_null))
		set_state(my_id, cur->name, cur->sym, &undefined);
}

static bool is_possibly_zero(const char *name, struct symbol *sym)
{
	struct sm_state *sm, *tmp;

	sm = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!sm)
		return false;
	FOR_EACH_PTR(sm->possible, tmp) {
		if (!estate_rl(tmp->state))
			continue;
		if (rl_min(estate_rl(tmp->state)).value == 0 &&
		    rl_max(estate_rl(tmp->state)).value == 0)
			return true;
	} END_FOR_EACH_PTR(tmp);

	return false;
}

static const char *get_allocation_fn_name(const char *name, struct symbol *sym)
{
	struct expression *expr;

	expr = get_assigned_expr_name_sym(name, sym);
	if (!expr)
		return "<unknown>";
	if (expr->type == EXPR_CALL &&
	    expr->fn->type == EXPR_SYMBOL &&
	    expr->fn->symbol && expr->fn->symbol->ident)
		return expr->fn->symbol->ident->name;

	return "<unknown>";
}

static void check_dereference_name_sym(char *name, struct symbol *sym)
{
	struct sm_state *sm;
	const char *fn_name;

	sm = get_sm_state(my_id, name, sym);
	if (!sm)
		return;
	if (is_ignored(my_id, sm->name, sm->sym))
		return;
	if (!is_possibly_zero(sm->name, sm->sym))
		return;
	if (is_impossible_path())
		return;
	if (!slist_has_state(sm->possible, &null))
		return;

	sm_msg("%s: sm='%s'", __func__, show_sm(sm));
	fn_name = get_allocation_fn_name(name, sym);
	sm_error("potential null dereference '%s'.  (%s returns null)",
		 sm->name, fn_name);
}

static void check_dereference(struct expression *expr)
{
	char *name;
	struct symbol *sym;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return;
	check_dereference_name_sym(name, sym);
	free_string(name);
}

static void match_dereferences(struct expression *expr)
{
	if (expr->type != EXPR_PREOP)
		return;
	check_dereference(expr->unop);
}

static void match_pointer_as_array(struct expression *expr)
{
	if (!is_array(expr))
		return;
	check_dereference(get_array_base(expr));
}

static void set_param_dereferenced(struct expression *call, struct expression *arg, char *key, char *unused)
{
	struct symbol *sym;
	char *name;

	name = get_variable_from_key(arg, key, &sym);
	if (!name || !sym)
		goto free;

	check_dereference_name_sym(name, sym);
free:
	free_string(name);
}

static int called_with_no_fail(struct expression *call, int param)
{
	struct expression *arg;
	sval_t sval;

	if (param == -1)
		return 0;
	call = strip_expr(call);
	if (call->type != EXPR_CALL)
		return 0;
	arg = get_argument_from_call_expr(call->args, param);
	if (get_value(arg, &sval) && (sval.uvalue & GFP_NOFAIL()))
		return 1;
	return 0;
}

static void match_assign_returns_null(const char *fn, struct expression *expr, void *_gfp)
{
	int gfp_param = PTR_INT(_gfp);

	if (called_with_no_fail(expr->right, gfp_param))
		return;
	set_state_expr(my_id, expr->left, &null);
}

static void register_allocation_funcs(void)
{
	char filename[256];
	struct token *token;
	const char *func;
	int arg;

	snprintf(filename, sizeof(filename), "%s.allocation_funcs_gfp", option_project_str);
	token = get_tokens_file(filename);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT) {
			printf("error parsing '%s' line %d\n", filename, token->pos.line);
			return;
		}
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) == TOKEN_IDENT)
			arg = -1;
		else if (token_type(token) == TOKEN_NUMBER)
			arg = atoi(token->number);
		else {
			printf("error parsing '%s' line %d\n", filename, token->pos.line);
			return;
		}
		add_function_assign_hook(func, &match_assign_returns_null, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_unchecked_allocation(int id)
{
	my_id = id;

	add_modification_hook(my_id, &is_ok);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_hook(&match_dereferences, DEREF_HOOK);
	add_hook(&match_pointer_as_array, OP_HOOK);
	select_return_implies_hook(DEREFERENCE, &set_param_dereferenced);
	register_allocation_funcs();
}
