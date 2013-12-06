/*
 * smatch/smatch_strlen.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <errno.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

#define UNKNOWN_SIZE (-1)

static int my_strlen_id;
/*
 * The trick with the my_equiv_id is that if we have:
 * foo = strlen(bar);
 * We don't know at that point what the strlen() is but we know it's equivalent
 * to "foo" so maybe we can find the value of "foo" later.
 */
static int my_equiv_id;

static struct smatch_state *size_to_estate(int size)
{
	sval_t sval;

	sval.type = &int_ctype;
	sval.value = size;

	return alloc_estate_sval(sval);
}

static struct smatch_state *unmatched_strlen_state(struct sm_state *sm)
{
	return size_to_estate(UNKNOWN_SIZE);
}

static void set_strlen_undefined(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(sm->owner, sm->name, sm->sym, size_to_estate(UNKNOWN_SIZE));
}

static void set_strlen_equiv_undefined(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(sm->owner, sm->name, sm->sym, &undefined);
}

static void match_string_assignment(struct expression *expr)
{
	struct range_list *rl;

	if (expr->op != '=')
		return;
	if (!get_implied_strlen(expr->right, &rl))
		return;
	set_state_expr(my_strlen_id, expr->left, alloc_estate_rl(clone_rl(rl)));
}

static void match_strlen(const char *fn, struct expression *expr, void *unused)
{
	struct expression *right;
	struct expression *str;
	struct expression *len_expr;
	char *len_name;
	struct smatch_state *state;

	right = strip_expr(expr->right);
	str = get_argument_from_call_expr(right->args, 0);
	len_expr = strip_expr(expr->left);

	len_name = expr_to_var(len_expr);
	if (!len_name)
		return;

	state = __alloc_smatch_state(0);
        state->name = len_name;
	state->data = len_expr;

	set_state_expr(my_equiv_id, str, state);
}

static int get_strlen_from_string(struct expression *expr, struct range_list **rl)
{
	sval_t sval;
	int len;

	len = expr->string->length;
	sval = sval_type_val(&int_ctype, len - 1);
	*rl = alloc_rl(sval, sval);
	return 1;
}


static int get_strlen_from_state(struct expression *expr, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state_expr(my_strlen_id, expr);
	if (!state)
		return 0;
	*rl = estate_rl(state);
	return 1;


}

static int get_strlen_from_equiv(struct expression *expr, struct range_list **rl)
{
	struct smatch_state *state;

	state = get_state_expr(my_equiv_id, expr);
	if (!state || !state->data)
		return 0;
	if (!get_implied_rl((struct expression *)state->data, rl))
		return 0;
	return 1;
}

int get_implied_strlen(struct expression *expr, struct range_list **rl)
{

	*rl = NULL;

	switch (expr->type) {
	case EXPR_STRING:
		return get_strlen_from_string(expr, rl);
	}

	if (get_strlen_from_state(expr, rl))
		return 1;
	if (get_strlen_from_equiv(expr, rl))
		return 1;
	return 0;
}

int get_size_from_strlen(struct expression *expr)
{
	struct range_list *rl;
	sval_t max;

	if (!get_implied_strlen(expr, &rl))
		return 0;
	max = rl_max(rl);
	if (sval_is_negative(max) || sval_is_max(max))
		return 0;

	return max.value + 1; /* add one because strlen doesn't include the NULL */
}

void register_strlen(int id)
{
	my_strlen_id = id;

	add_unmatched_state_hook(my_strlen_id, &unmatched_strlen_state);

	add_hook(&match_string_assignment, ASSIGNMENT_HOOK);

	add_modification_hook(my_strlen_id, &set_strlen_undefined);
	add_merge_hook(my_strlen_id, &merge_estates);
}

void register_strlen_equiv(int id)
{
	my_equiv_id = id;
	add_function_assign_hook("strlen", &match_strlen, NULL);
	add_modification_hook(my_equiv_id, &set_strlen_equiv_undefined);
}

