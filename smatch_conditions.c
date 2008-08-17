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

int __negate = 0;
int __ors = 0;
int __ands = 0;

static void split_conditions(struct expression *expr);

static int is_logical_and(struct expression *expr)
{
	/* If you have if (!(a && b)) smatch translates that to 
	 * if (!a || !b).  Logically those are the same.
	 */

	if ((!__negate && expr->op == SPECIAL_LOGICAL_AND) ||
	    (__negate && expr->op == SPECIAL_LOGICAL_OR))
		return 1;
	return 0;
}

static int is_logical_or(struct expression *expr)
{
	if ((!__negate && expr->op == SPECIAL_LOGICAL_OR) ||
	     (__negate && expr->op == SPECIAL_LOGICAL_AND))
		return 1;
	return 0;
}

static void inc_ands_ors(struct expression *expr)
{
	if (is_logical_and(expr))
		__ands++;
	else if (is_logical_or(expr))
		__ors++;
}

static void dec_ands_ors(struct expression *expr)
{
	if (is_logical_and(expr))
		__ands--;
	else if (is_logical_or(expr))
		__ors--;
}

static void special_kernel_macros(struct expression *expr)
{
#ifdef KERNEL
	struct expression *tmp;

	tmp = first_ptr_list((struct ptr_list *) expr->args);
	if (tmp->op == '!' && tmp->unop->op == '!' 
	    && tmp->unop->unop->op == '(') {
		split_conditions(tmp->unop->unop->unop);
	} else {
		__pass_to_client(expr, CONDITION_HOOK);	
		__split_expr(expr);
		return;
	}

#endif
}

static int is_zero(struct expression *expr)
{
	if (expr->type == EXPR_VALUE && expr->value == 0)
		return 1;
	if (expr->op == '(')
		return is_zero(expr->unop);
	if (expr->type == EXPR_CAST) 
		return is_zero(expr->cast_expression);
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
		__negate = (__negate +  1)%2;
		split_conditions(tmp);
		__negate = (__negate +  1)%2;
		return 1;
	}

	return 0;
}

static void split_conditions(struct expression *expr)
{
	static int __ors_reached;

	SM_DEBUG("%d in split_conditions type=%d\n", get_lineno(), expr->type);
  
	if (expr->type == EXPR_COMPARE)
		if (handle_zero_comparisons(expr))
			return;

	if (expr->type == EXPR_LOGICAL) {
		unsigned int path_orig;

		inc_ands_ors(expr);
		path_orig = __split_path_id();
		__split_false_states_mini();

		split_conditions(expr->left);
		
		if (is_logical_and(expr)) {
			__split_path_id();
			__pop_false_states_mini();
			split_conditions(expr->right);
		} else if (is_logical_or(expr)) {
			if (!__ors_reached) {
				__ors_reached = 1;
				__first_and_clump();
			} else {
				__merge_and_clump();
			}
			__split_path_id();
			__use_false_states_mini();
			split_conditions(expr->right);
		}
		dec_ands_ors(expr);
		
		if (__ands + __ors == 0) {
			__merge_and_clump();
			__use_and_clumps();
			__ors_reached = 0;
		}

		__restore_path_id(path_orig);
		return;
	} else if (expr->type == EXPR_PREOP && expr->op == '!') {
		__negate = (__negate +  1)%2;
		split_conditions(expr->unop);
		__negate = (__negate +  1)%2;
		return;
	} else if (expr->type == EXPR_PREOP && expr->op == '(') {
		split_conditions(expr->unop);
	} else if (expr->type == EXPR_CALL) {

		if (expr->fn->type != EXPR_SYMBOL || 
		    strcmp("__builtin_expect", expr->fn->symbol_name->name)) {
			__pass_to_client(expr, CONDITION_HOOK);	
			__split_expr(expr);
			return;
		}
		special_kernel_macros(expr);
	} else {
		__pass_to_client(expr, CONDITION_HOOK);	
		__split_expr(expr);
	}
}

void __split_whole_condition(struct expression *expr)
{

	split_conditions(expr);
	SM_DEBUG("%d __ands = %d __ors = %d __negate = %d\n", get_lineno(),
		 __ands, __ors, __negate);
}
