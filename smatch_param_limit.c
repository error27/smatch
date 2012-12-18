/*
 * sparse/smatch_param_limit.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "scope.h"
#include "smatch.h"
#include "smatch_extra.h"

static int orig_id;
static int modify_id;
static int side_effects;

static struct smatch_state *orig_states[32];

STATE(modified);

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return extra_undefined(estate_type(sm->state));
}

int was_modified_sym(struct symbol *sym)
{
	if (!side_effects)
		return 0;
	if (!sym || !sym->ident)
		return 1;  /* safer to say it was modified? */
	if (get_state(modify_id, sym->ident->name, sym))
		return 1;
	return 0;
}

static int is_local(struct symbol *sym)
{
	if (!sym->scope || !sym->scope->token)
		return 0;
	return 1;
}

static void check_expr(struct expression *expr)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	name = get_variable_from_expr_complex(expr, &sym);
	if (!sym) {
		side_effects = 1;
		goto free;
	}

	if (!is_local(sym))
		side_effects = 1;

	/*
	 * Pointers are so tricky to handle just bail.
	 */
	if (get_real_base_type(sym)->type == SYM_PTR)
		side_effects = 1;

	/*
	 * TODO: it should only do this if we modify something that's not on the
	 * stack.
	 */
	if (get_param_num_from_sym(sym) >= 0) {
		side_effects = 1;
		if (!get_state_expr(orig_id, expr)) {
			state = get_implied_estate(expr);
			set_state_expr(orig_id, expr, state);
		}
	}

	set_state_expr(modify_id, expr, &modified);
free:
	free_string(name);
}

static void match_assign(struct expression *expr)
{
	check_expr(expr->left);
}

static void unop_expr(struct expression *expr)
{
	if (expr->op != SPECIAL_DECREMENT && expr->op != SPECIAL_INCREMENT)
		return;

	expr = strip_expr(expr->unop);
	check_expr(expr);
}

static int no_effect_func;
static int db_no_effect_func(void *unused, int argc, char **argv, char **azColName)
{
	no_effect_func = 1;
	return 0;
}

static int is_no_side_effect_func(struct expression *fn)
{
	static char sql_filter[1024];

	if (fn->type != EXPR_SYMBOL || !fn->symbol)
		return 0;

	if (fn->symbol->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 get_filename(), fn->symbol->ident->name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s' and static = 0;",
			 fn->symbol->ident->name);
	}

	no_effect_func = 0;
	run_sql(db_no_effect_func, "select * from no_side_effects where %s", sql_filter);

	return no_effect_func;
}

static void match_call(struct expression *expr)
{
	struct expression *arg, *tmp;

	if (!is_no_side_effect_func(expr->fn))
		side_effects = 1;

	FOR_EACH_PTR(expr->args, arg) {
		tmp = strip_expr(arg);
		if (tmp->type != EXPR_PREOP || tmp->op != '&')
			continue;
		tmp = strip_expr(tmp->unop);
		check_expr(expr);
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
			check_expr(expr);
			continue;
		}
	} END_FOR_EACH_PTR(expr);
}

struct smatch_state *get_orig_estate(struct symbol *sym)
{
	struct smatch_state *state;

	if (!sym->ident || !sym->ident->name)
		return NULL;

	state = get_state(orig_id, sym->ident->name, sym);
	if (state)
		return state;

	return get_state(SMATCH_EXTRA, sym->ident->name, sym);
}

static void match_after_def(struct symbol *sym)
{
	struct smatch_state *state;
	struct symbol *tmp;
	int param;

	param = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		param++;
		if (param >= 32)
			return;

		orig_states[param] = NULL;
		state = get_orig_estate(tmp);
		if (!state)
			continue;
		orig_states[param] = state;
	} END_FOR_EACH_PTR(tmp);
}

static void print_return_value_param(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct smatch_state *state;
	struct symbol *tmp;
	int param;

	param = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		param++;
		state = get_orig_estate(tmp);
		if (!state)
			continue;
		if (param < 32 &&
		    range_lists_equiv(estate_ranges(orig_states[param]), estate_ranges(state)))
			continue;
		sm_msg("info: return_limited_param %d %d '%s' '$$' '%s' %s",
		       return_id, param, return_ranges,
		       state->name, global_static());
	} END_FOR_EACH_PTR(tmp);
}

static void match_end_func(struct symbol *sym)
{
	if (option_info && !side_effects)
		sm_msg("info: no_side_effects %s", global_static());
	side_effects = 0;
}

void register_param_limit(int id)
{
	modify_id = id;

	add_hook(&match_after_def, AFTER_DEF_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&unop_expr, OP_HOOK);
	add_hook(&match_call, FUNCTION_CALL_HOOK);
	add_hook(&asm_expr, ASM_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
	add_returned_state_callback(&print_return_value_param);
}

void register_param_limit2(int id)
{
	orig_id = id;
	add_merge_hook(orig_id, &merge_estates);
	add_unmatched_state_hook(orig_id, &unmatched_state);
}

