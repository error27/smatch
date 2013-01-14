/*
 * sparse/check_bool_implications.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

static int my_id;

static struct state_list_stack *true_stack;
static struct state_list_stack *false_stack;

static int returns_other;

static int non_bool(struct expression *expr)
{
	if (possibly_true(expr, '<', zero_expr()))
		return 1;
	if (possibly_true(expr, '>', value_expr(1)))
		return 1;
	return 0;
}

static void handle_condition_return(struct expression *expr)
{
	struct state_list *slist;
	int was_final_pass;

	was_final_pass = final_pass;
	final_pass = 0;
	__push_fake_cur_slist();
	__split_whole_condition(expr);
	push_slist(&true_stack, clone_slist(__get_cur_slist()));
	__push_true_states();
	__use_false_states();
	push_slist(&false_stack, clone_slist(__get_cur_slist()));
	__merge_true_states();
	slist = __pop_fake_cur_slist();
	final_pass = was_final_pass;
	free_slist(&slist);
}

static void match_return(struct expression *ret_value)
{
	sval_t sval;

	if (returns_other || !ret_value)
		return;

	if (non_bool(ret_value)) {
		returns_other = 1;
		return;
	}

	if (get_implied_value(ret_value, &sval) && sval.value == 1) {
		push_slist(&true_stack, clone_slist(__get_cur_slist()));
		return;
	}

	if (get_implied_value(ret_value, &sval) && sval.value == 0) {
		push_slist(&false_stack, clone_slist(__get_cur_slist()));
		return;
	}

	handle_condition_return(ret_value);
}

static void print_implications(struct symbol *sym, int param,
		struct state_list *true_states,
		struct state_list *false_states)
{
	struct smatch_state *true_state;
	struct smatch_state *false_state;

	if (!sym->ident)
		return;

	true_state = get_state_slist(true_states, SMATCH_EXTRA, sym->ident->name, sym);
	false_state = get_state_slist(false_states, SMATCH_EXTRA, sym->ident->name, sym);

	if (!true_state || !false_state)
		return;

	if (rl_equiv(estate_rl(true_state), estate_rl(false_state)))
		return;

	sm_msg("info: bool_return_implication \"1\" %d \"%s\" %s", param,
	       show_rl(estate_rl(true_state)), global_static());
	sm_msg("info: bool_return_implication \"0\" %d \"%s\" %s", param,
	       show_rl(estate_rl(false_state)), global_static());
}

static void cleanup(void)
{
	free_stack_and_slists(&true_stack);
	free_stack_and_slists(&false_stack);
	returns_other = 0;
}

static void match_end_func(struct symbol *sym)
{
	struct state_list *merged_true = NULL;
	struct state_list *merged_false = NULL;
	struct state_list *tmp;
	struct symbol *param_sym;
	int i;

	if (returns_other) {
		cleanup();
		return;
	}

	FOR_EACH_PTR(true_stack, tmp) {
		merge_slist(&merged_true, tmp);
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(false_stack, tmp) {
		merge_slist(&merged_false, tmp);
	} END_FOR_EACH_PTR(tmp);

	i = 0;
	FOR_EACH_PTR(sym->ctype.base_type->arguments, param_sym) {
		print_implications(param_sym, i, merged_true, merged_false);
		i++;
	} END_FOR_EACH_PTR(param_sym);


	free_slist(&merged_true);
	free_slist(&merged_false);

	cleanup();
}

void check_bool_implications(int id)
{
	if (!option_info)
		return;

	my_id = id;
	add_hook(&match_return, RETURN_HOOK);
	add_hook(&match_end_func, END_FUNC_HOOK);
}
