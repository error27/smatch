/*
 * sparse/smatch_conditions.c
 *
 * Copyright (C) 2006,2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

/*
 * The simplest type of condition is
 * if (a) { ...
 *
 * The next simplest kind of conditions is
 * if (a && b) { c;
 * In that case 'a' is true when we get to 'b' and both are true
 * when we get to c.
 *
 * Or's are a little more complicated.
 * if (a || b) { c;
 * We know 'a' is not true when we get to 'b' but it may be true
 * when we get to c.  
 *
 * If we mix and's and or's that's even more complicated.
 * if (a && b && c || a && d) { d ;
 * 'a' is true when we get to 'b', 'c' and 'd'.
 * 'b' is true when we reach 'c' but otherwise we don't know.
 * 
 * The other thing that complicates matters is if we negate
 * some if conditions.
 * if (!a) { ...
 * We pass the un-negated version to the client and flip the true
 * and false values internally.
 * 
 * And negations can be part of a compound.
 * if (a && !(b || c)) { d;
 * In that situation we multiply the negative through to simplify
 * stuff so that we can remove the parens like this:
 * if (a && !b && !c) { d;
 *
 * One other thing is that:
 * if ((a) != 0){ ...
 * that's basically the same as testing for just 'a' so we simplify
 * it before passing it to the script.
 */

#include "smatch.h"

#define KERNEL

static void split_conditions(struct expression *expr);

static int is_logical_and(struct expression *expr)
{
	if (expr->op == SPECIAL_LOGICAL_AND)
		return 1;
	return 0;
}

static int handle_zero_comparisons(struct expression *expr)
{
	struct expression *tmp = NULL;

	// if left is zero or right is zero
	if (is_zero(expr->left))
		tmp = expr->right;
	else if (is_zero(expr->right))
		tmp = expr->left;
	else
		return 0;

	// "if (foo != 0)" is the same as "if (foo)"
	if (expr->op == SPECIAL_NOTEQUAL) {
		split_conditions(tmp);
		return 1;
	}

	// "if (foo == 0)" is the same as "if (!foo)"
	if (expr->op == SPECIAL_EQUAL) {
		split_conditions(tmp);
		__negate_cond_stacks();
		return 1;
	}

	return 0;
}

/*
 * This function is for handling calls to likely/unlikely
 */

static int ignore_builtin_expect(struct expression *expr)
{
	if (sym_name_is("__builtin_expect", expr->fn)) {
		split_conditions(first_ptr_list((struct ptr_list *) expr->args));
		return 1;
	}
	if (sym_name_is("__builtin_constant_p", expr->fn)) {
		split_conditions(first_ptr_list((struct ptr_list *) expr->args));
		return 1;
	}
	return 0;
}

static int handle_preop(struct expression *expr)
{
	if (expr->op == '!') {
		split_conditions(expr->unop);
		__negate_cond_stacks();
		return 1;
	}
	return 0;
}

static void handle_logical(struct expression *expr)
{
	/*
	 * If we come to an "and" expr then:
	 * We split the left side.
	 * We keep all the current states.
	 * We split the right side.
	 * We keep all the states from both true sides.
	 *
	 * If it's an "or" expr then:
	 * We save the current slist.
	 * We split the left side.
	 * We use the false states for the right side.
	 * We split the right side.
	 * We save all the states that are the same on both sides.
	 */

	split_conditions(expr->left);

	if (is_logical_and(expr)) {
		__use_cond_true_states();
	} else {
		__use_cond_false_states();
	}
	
	__save_pre_cond_states();
	__push_cond_stacks();

	split_conditions(expr->right);

	if (is_logical_and(expr)) {
		__and_cond_states();
	} else {
		__or_cond_states();
	}

	__pop_pre_cond_states();
	__use_cond_true_states();
}

static void handle_select(struct expression *expr)
{
	/*
	 * if ((aaa()?bbb():ccc())) { ...
	 *
	 * This is almost the same as:
	 * if ((aaa() && bbb()) || (!aaa() && ccc())) { ...
	 *
	 * It's a bit complicated because we shouldn't pass aaa()
	 * to the clients more than once.
	 */

	split_conditions(expr->conditional);

	__save_false_states_for_later();
	__use_cond_true_states();

	if (known_condition_true(expr->cond_true)) {
		__split_expr(expr->cond_true);
	} else {
		__push_cond_stacks();
		split_conditions(expr->cond_true);
		__and_cond_states();
	}

	if (known_condition_false(expr->cond_false)) {
		__pop_pre_cond_states();
		__use_cond_true_states();
		return;
	}

	__use_previously_stored_false_states();

	__save_pre_cond_states();
	__push_cond_stacks();
	split_conditions(expr->cond_false);
	__or_cond_states();
	__pop_pre_cond_states();

	__use_cond_true_states();
}

static void split_conditions(struct expression *expr)
{

	SM_DEBUG("%d in split_conditions type=%d\n", get_lineno(), expr->type);

	expr = strip_expr(expr);
	if (!expr)
		return;

	switch(expr->type) {
	case EXPR_LOGICAL:
		handle_logical(expr);
		return;
	case EXPR_COMPARE:
		if (handle_zero_comparisons(expr))
			return;
		break;
	case EXPR_CALL:
		if (ignore_builtin_expect(expr))
			return;
		break;
	case EXPR_PREOP:
		if (handle_preop(expr))
			return;
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		handle_select(expr);
		return;
	}

	__pass_to_client(expr, CONDITION_HOOK);	
	__split_expr(expr);
}

static int inside_condition;
void __split_whole_condition(struct expression *expr)
{
	SM_DEBUG("%d in __split_whole_condition\n", get_lineno());
	inside_condition++;
	__save_pre_cond_states();
	__push_cond_stacks();
	if (expr)
		split_conditions(expr);
	__use_cond_states();
	__pass_to_client(expr, WHOLE_CONDITION_HOOK);
	inside_condition--;
}

int in_condition()
{
	return inside_condition;
}
