/*
 * Copyright (C) 2006,2008 Dan Carpenter.
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
 * 'a' is true when we evaluate 'b', and 'd'.
 * 'b' is true when we evaluate 'c' but otherwise we don't.
 *
 * The other thing that complicates matters is if we negate
 * some if conditions.
 * if (!a) { ...
 * Smatch has passes the un-negated version to the client and flip
 * the true and false values internally.  This makes it easier
 * to write checks.
 *
 * And negations can be part of a compound.
 * if (a && !(b || c)) { d;
 * In that situation we multiply the negative through to simplify
 * stuff so that we can remove the parens like this:
 * if (a && !b && !c) { d;
 *
 * One other thing is that:
 * if ((a) != 0){ ...
 * that's basically the same as testing for just 'a' and we simplify
 * comparisons with zero before passing it to the script.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"
#include "smatch_expression_stacks.h"

extern int __expr_stmt_count;

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
	return 0;
}

/*
 * handle_compound_stmt() is for: foo = ({blah; blah; blah; 1})
 */

static void handle_compound_stmt(struct statement *stmt)
{
	struct expression *expr = NULL;
	struct statement *last;
	struct statement *s;

	last = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (last->type != STMT_EXPRESSION)
		last = NULL;
	else
		expr = last->expression;

	FOR_EACH_PTR(stmt->stmts, s) {
		if (s != last)
			__split_stmt(s);
	} END_FOR_EACH_PTR(s);
	split_conditions(expr);
	return;
}

static int handle_preop(struct expression *expr)
{
	struct statement *stmt;

	if (expr->op == '!') {
		split_conditions(expr->unop);
		__negate_cond_stacks();
		return 1;
	}
	stmt = get_expression_statement(expr);
	if (stmt) {
		handle_compound_stmt(stmt);
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
	__process_post_op_stack();

	if (!is_logical_and(expr))
		__use_cond_false_states();

	__push_cond_stacks();

	__save_pre_cond_states();
	split_conditions(expr->right);
	__process_post_op_stack();
	__discard_pre_cond_states();

	if (is_logical_and(expr))
		__and_cond_states();
	else
		__or_cond_states();

	__use_cond_true_states();
}

static struct state_list *combine(struct state_list *orig, struct state_list *fake,
				struct state_list *new)
{
	struct state_list *ret = NULL;

	overwrite_slist(orig, &ret);
	overwrite_slist(fake, &ret);
	overwrite_slist(new, &ret);
	free_slist(&new);

	return ret;
}

/*
 * handle_select()
 * if ((aaa()?bbb():ccc())) { ...
 *
 * This is almost the same as:
 * if ((aaa() && bbb()) || (!aaa() && ccc())) { ...
 *
 * It's a bit complicated because we shouldn't pass aaa()
 * to the clients more than once.
 */

static void handle_select(struct expression *expr)
{
	struct state_list *a_T = NULL;
	struct state_list *a_F = NULL;
	struct state_list *a_T_b_T = NULL;
	struct state_list *a_T_b_F = NULL;
	struct state_list *a_T_b_fake = NULL;
	struct state_list *a_F_c_T = NULL;
	struct state_list *a_F_c_F = NULL;
	struct state_list *a_F_c_fake = NULL;
	struct sm_state *sm;

	/*
	 * Imagine we have this:  if (a ? b : c) { ...
	 *
	 * The condition is true if "a" is true and "b" is true or
	 * "a" is false and "c" is true.  It's false if "a" is true
	 * and "b" is false or "a" is false and "c" is false.
	 *
	 * The variable name "a_T_b_T" stands for "a true b true" etc.
	 *
	 * But if we know "b" is true then we can simpilify things.
	 * The condition is true if "a" is true or if "a" is false and
	 * "c" is true.  The only way the condition can be false is if
	 * "a" is false and "c" is false.
	 *
	 * The remaining thing is the "a_T_b_fake".  When we simplify
	 * the equations we have to take into consideration that other
	 * states may have changed that don't play into the true false
	 * equation.  Take the following example:
	 * if ({
	 *         (flags) = __raw_local_irq_save();
	 *         _spin_trylock(lock) ? 1 :
	 *                 ({ raw_local_irq_restore(flags);  0; });
	 *    })
	 * Smatch has to record that the irq flags were restored on the
	 * false path.
	 *
	 */

	__save_pre_cond_states();

	split_conditions(expr->conditional);

	a_T = __copy_cond_true_states();
	a_F = __copy_cond_false_states();

	__push_cond_stacks();
	__push_fake_cur_slist();
	split_conditions(expr->cond_true);
	__process_post_op_stack();
	a_T_b_fake = __pop_fake_cur_slist();
	a_T_b_T = combine(a_T, a_T_b_fake, __pop_cond_true_stack());
	a_T_b_F = combine(a_T, a_T_b_fake, __pop_cond_false_stack());

	__use_cond_false_states();

	__push_cond_stacks();
	__push_fake_cur_slist();
	split_conditions(expr->cond_false);
	a_F_c_fake = __pop_fake_cur_slist();
	a_F_c_T = combine(a_F, a_F_c_fake, __pop_cond_true_stack());
	a_F_c_F = combine(a_F, a_F_c_fake, __pop_cond_false_stack());

	/* We have to restore the pre condition states so that
	   implied_condition_true() will use the right cur_slist */
	__use_pre_cond_states();

	if (implied_condition_true(expr->cond_true)) {
		free_slist(&a_T_b_T);
		free_slist(&a_T_b_F);
		a_T_b_T = clone_slist(a_T);
		overwrite_slist(a_T_b_fake, &a_T_b_T);
	}
	if (implied_condition_false(expr->cond_true)) {
		free_slist(&a_T_b_T);
		free_slist(&a_T_b_F);
		a_T_b_F = clone_slist(a_T);
		overwrite_slist(a_T_b_fake, &a_T_b_F);
	}
	if (implied_condition_true(expr->cond_false)) {
		free_slist(&a_F_c_T);
		free_slist(&a_F_c_F);
		a_F_c_T = clone_slist(a_F);
		overwrite_slist(a_F_c_fake, &a_F_c_T);
	}
	if (implied_condition_false(expr->cond_false)) {
		free_slist(&a_F_c_T);
		free_slist(&a_F_c_F);
		a_F_c_F = clone_slist(a_F);
		overwrite_slist(a_F_c_fake, &a_F_c_F);
	}

	merge_slist(&a_T_b_T, a_F_c_T);
	merge_slist(&a_T_b_F, a_F_c_F);

	__pop_cond_true_stack();
	__pop_cond_false_stack();
	__push_cond_stacks();
	FOR_EACH_PTR(a_T_b_T, sm) {
		__set_true_false_sm(sm, NULL);
	} END_FOR_EACH_PTR(sm);
	FOR_EACH_PTR(a_T_b_F, sm) {
		__set_true_false_sm(NULL, sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&a_T_b_fake);
	free_slist(&a_F_c_fake);
}

static int make_op_unsigned(int op)
{
	switch (op) {
	case '<':
		return SPECIAL_UNSIGNED_LT;
	case SPECIAL_LTE:
		return SPECIAL_UNSIGNED_LTE;
	case '>':
		return SPECIAL_UNSIGNED_GT;
	case SPECIAL_GTE:
		return SPECIAL_UNSIGNED_GTE;
	}
	return op;
}

static void hackup_unsigned_compares(struct expression *expr)
{
	if (expr->type != EXPR_COMPARE)
		return;

	if (type_unsigned(get_type(expr)))
		expr->op = make_op_unsigned(expr->op);
}

static void split_conditions(struct expression *expr)
{
	if (option_debug) {
		char *cond = expr_to_str(expr);

		sm_debug("%d in split_conditions (%s)\n", get_lineno(), cond);
		free_string(cond);
	}

	expr = strip_expr(expr);
	if (!expr)
		return;

	switch (expr->type) {
	case EXPR_LOGICAL:
		__pass_to_client(expr, LOGIC_HOOK);
		handle_logical(expr);
		return;
	case EXPR_COMPARE:
		hackup_unsigned_compares(expr);
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

	/* fixme: this should be in smatch_flow.c
	   but because of the funny stuff we do with conditions
	   it's awkward to put it there.  We would need to
	   call CONDITION_HOOK in smatch_flow as well.
	*/
	push_expression(&big_expression_stack, expr);
	if (expr->type == EXPR_COMPARE) {
		if (expr->left->type != EXPR_POSTOP)
			__split_expr(expr->left);
		if (expr->right->type != EXPR_POSTOP)
			__split_expr(expr->right);
	} else if (expr->type != EXPR_POSTOP) {
		__split_expr(expr);
	}
	__pass_to_client(expr, CONDITION_HOOK);
	if (expr->type == EXPR_COMPARE) {
		if (expr->left->type == EXPR_POSTOP)
			__split_expr(expr->left);
		if (expr->right->type == EXPR_POSTOP)
			__split_expr(expr->right);
	} else if (expr->type == EXPR_POSTOP) {
		__split_expr(expr);
	}
	__process_post_op_stack();
	pop_expression(&big_expression_stack);
}

static int inside_condition;
void __split_whole_condition(struct expression *expr)
{
	sm_debug("%d in __split_whole_condition\n", get_lineno());
	inside_condition++;
	__save_pre_cond_states();
	__push_cond_stacks();
	/* it's a hack, but it's sometimes handy to have this stuff
	   on the big_expression_stack.  */
	push_expression(&big_expression_stack, expr);
	if (expr)
		split_conditions(expr);
	__use_cond_states();
	__pass_to_client(expr, WHOLE_CONDITION_HOOK);
	pop_expression(&big_expression_stack);
	inside_condition--;
	sm_debug("%d done __split_whole_condition\n", get_lineno());
}

void __handle_logic(struct expression *expr)
{
	sm_debug("%d in __handle_logic\n", get_lineno());
	inside_condition++;
	__save_pre_cond_states();
	__push_cond_stacks();
	/* it's a hack, but it's sometimes handy to have this stuff
	   on the big_expression_stack.  */
	push_expression(&big_expression_stack, expr);
	if (expr)
		split_conditions(expr);
	__use_cond_states();
	__pass_to_client(expr, WHOLE_CONDITION_HOOK);
	pop_expression(&big_expression_stack);
	__merge_false_states();
	inside_condition--;
	sm_debug("%d done __handle_logic\n", get_lineno());
}

int is_condition(struct expression *expr)
{

	expr = strip_expr(expr);
	if (!expr)
		return 0;

	switch (expr->type) {
	case EXPR_LOGICAL:
	case EXPR_COMPARE:
		return 1;
	case EXPR_PREOP:
		if (expr->op == '!')
			return 1;
	}
	return 0;
}

int __handle_condition_assigns(struct expression *expr)
{
	struct expression *right;

	right = strip_expr(expr->right);
	if (!is_condition(expr->right))
		return 0;

	sm_debug("%d in __handle_condition_assigns\n", get_lineno());
	inside_condition++;
	__save_pre_cond_states();
	__push_cond_stacks();
	/* it's a hack, but it's sometimes handy to have this stuff
	   on the big_expression_stack.  */
	push_expression(&big_expression_stack, right);
	split_conditions(right);
	__use_cond_states();
	set_extra_expr_mod(expr->left, alloc_estate_sval(sval_type_val(get_type(expr->left), 1)));
	__pass_to_client(right, WHOLE_CONDITION_HOOK);
	pop_expression(&big_expression_stack);
	inside_condition--;
	sm_debug("%d done __handle_condition_assigns\n", get_lineno());

	__push_true_states();
	__use_false_states();
	set_extra_expr_mod(expr->left, alloc_estate_sval(sval_type_val(get_type(expr->left), 0)));
	__merge_true_states();
	__pass_to_client(expr, ASSIGNMENT_HOOK);
	return 1;
}

static int is_select_assign(struct expression *expr)
{
	struct expression *right;

	if (expr->op != '=')
		return 0;
	right = strip_expr(expr->right);
	if (right->type == EXPR_CONDITIONAL)
		return 1;
	if (right->type == EXPR_SELECT)
		return 1;
	return 0;
}

static void set_fake_assign(struct expression *new,
			struct expression *left, int op, struct expression *right)
{

	new->pos = left->pos;
	new->op = op;
	new->type = EXPR_ASSIGNMENT;
	new->left = left;
	new->right = right;
}

int __handle_select_assigns(struct expression *expr)
{
	struct expression *right;
	struct state_list *final_states = NULL;
	struct sm_state *sm;
	int is_true;
	int is_false;

	if (!is_select_assign(expr))
		return 0;
	sm_debug("%d in __handle_ternary_assigns\n", get_lineno());
	right = strip_expr(expr->right);

	is_true = implied_condition_true(right->conditional);
	is_false = implied_condition_false(right->conditional);

	/* hah hah.  the ultra fake out */
	__save_pre_cond_states();
	__split_whole_condition(right->conditional);

	if (!is_false) {
		struct expression fake_expr;

		if (right->cond_true)
			set_fake_assign(&fake_expr, expr->left, expr->op, right->cond_true);
		else
			set_fake_assign(&fake_expr, expr->left, expr->op, right->conditional);
		__split_expr(&fake_expr);
		final_states = clone_slist(__get_cur_slist());
	}

	__use_false_states();
	if (!is_true) {
		struct expression fake_expr;

		set_fake_assign(&fake_expr, expr->left, expr->op, right->cond_false);
		__split_expr(&fake_expr);
		merge_slist(&final_states, __get_cur_slist());
	}

	__use_pre_cond_states();

	FOR_EACH_PTR(final_states, sm) {
		__set_sm(sm);
	} END_FOR_EACH_PTR(sm);

	free_slist(&final_states);

	sm_debug("%d done __handle_ternary_assigns\n", get_lineno());

	return 1;
}

static struct statement *split_then_return_last(struct statement *stmt)
{
	struct statement *tmp;
	struct statement *last_stmt;

	last_stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (!last_stmt)
		return NULL;

	__push_scope_hooks();
	FOR_EACH_PTR(stmt->stmts, tmp) {
		if (tmp == last_stmt)
			return last_stmt;
		__split_stmt(tmp);
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

int __handle_expr_statement_assigns(struct expression *expr)
{
	struct expression *right;
	struct statement *stmt;

	right = expr->right;
	if (right->type == EXPR_PREOP && right->op == '(')
		right = right->unop;
	if (right->type != EXPR_STATEMENT)
		return 0;

	__expr_stmt_count++;
	stmt = right->statement;
	if (stmt->type == STMT_COMPOUND) {
		struct statement *last_stmt;
		struct expression fake_assign;
		struct expression fake_expr_stmt;

		last_stmt = split_then_return_last(stmt);
		if (!last_stmt) {
			__expr_stmt_count--;
			return 0;
		}

		fake_expr_stmt.pos = last_stmt->pos;
		fake_expr_stmt.type = EXPR_STATEMENT;
		fake_expr_stmt.statement = last_stmt;

		fake_assign.pos = last_stmt->pos;
		fake_assign.op = expr->op;
		fake_assign.type = EXPR_ASSIGNMENT;
		fake_assign.left = expr->left;
		fake_assign.right = &fake_expr_stmt;

		__split_expr(&fake_assign);

		__call_scope_hooks();
	} else if (stmt->type == STMT_EXPRESSION) {
		struct expression fake_assign;

		fake_assign.pos = stmt->pos;
		fake_assign.op = expr->op;
		fake_assign.type = EXPR_ASSIGNMENT;
		fake_assign.left = expr->left;
		fake_assign.right = stmt->expression;

		__split_expr(&fake_assign);

	} else {
		__split_stmt(stmt);
	}
	__expr_stmt_count--;
	return 1;
}

int in_condition(void)
{
	return inside_condition;
}
