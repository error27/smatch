/*
 * sparse/smatch_extra.c
 *
 * Copyright (C) 2008 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

struct data_info unknown_num = {
	.type = DATA_NUM,
	.merged = 0,
	.values = NULL,
};

static struct smatch_state extra_undefined = {
	.name = "unknown",
	.data = &unknown_num,
};

static struct smatch_state *alloc_extra_state_no_name(int val)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->data = (void *)alloc_data_info(val);
	return state;
}

static struct smatch_state *alloc_extra_state(int val)
{
	struct smatch_state *state;
	static char name[20];

	if (val == UNDEFINED)
		return &extra_undefined;

	state = alloc_extra_state_no_name(val);
	snprintf(name, 20, "%d", val);
	state->name = alloc_string(name);
	return state;
}

static struct smatch_state *merge_func(const char *name, struct symbol *sym,
				       struct smatch_state *s1,
				       struct smatch_state *s2)
{
	struct data_info *info1 = (struct data_info *)s1->data;
	struct data_info *info2 = (struct data_info *)s2->data;
	struct smatch_state *tmp;

	tmp = alloc_extra_state_no_name(UNDEFINED);
	tmp->name = "extra_merged";
	((struct data_info *)tmp->data)->merged = 1;
	((struct data_info *)tmp->data)->values = 
		num_list_union(info1->values, info2->values);
	return tmp;
}

struct sm_state *__extra_merge(struct sm_state *one, struct state_list *slist1,
			       struct sm_state *two, struct state_list *slist2)
{
	struct data_info *info1;
	struct data_info *info2;

	if (!one->state->data || !two->state->data) {
		smatch_msg("internal error in smatch extra '%s = %s or %s'",
			   one->name, show_state(one->state),
			   show_state(two->state));
		return alloc_state(one->name, one->owner, one->sym,
				   &extra_undefined);
	}

	info1 = (struct data_info *)one->state->data;
	info2 = (struct data_info *)two->state->data;

	if (!info1->merged)
		free_stack(&one->my_pools);
	if (!info2->merged)
		free_stack(&two->my_pools);

	if (one == two && !one->my_pools) {
		add_pool(&one->my_pools, slist1);
		add_pool(&one->my_pools, slist2);
	} else {
		if (!one->my_pools)
			add_pool(&one->my_pools, slist1);
		if (!two->my_pools)
			add_pool(&two->my_pools, slist2);
	}

	add_pool(&one->all_pools, slist1);
	add_pool(&two->all_pools, slist2);
	return merge_sm_states(one, two);
}

struct sm_state *__extra_and_merge(struct sm_state *sm,
				     struct state_list_stack *stack)
{
	struct state_list *slist;
	struct sm_state *ret = NULL;
	struct sm_state *tmp;
	int i = 0;

	FOR_EACH_PTR(stack, slist) {
		if (!i++) {
			ret = get_sm_state_slist(slist, sm->name, sm->owner,
						 sm->sym);
		} else {
			tmp = get_sm_state_slist(slist, sm->name, sm->owner,
						 sm->sym);
			ret = merge_sm_states(ret, tmp);
		}
	} END_FOR_EACH_PTR(slist);
	if (!ret) {
		smatch_msg("Internal error in __extra_and_merge");
		return NULL;
	}
	ret->my_pools = stack;
	ret->all_pools = clone_stack(stack);
	return ret;
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return &extra_undefined;
}

static void match_function_call(struct expression *expr)
{
	struct expression *tmp;
	struct symbol *sym;
	char *name;
	int i = 0;

	FOR_EACH_PTR(expr->args, tmp) {
		if (tmp->op == '&') {
			name = get_variable_from_expr(tmp->unop, &sym);
			if (name) {
				set_state(name, my_id, sym, &extra_undefined);
			}
			free_string(name);
		}
		i++;
	} END_FOR_EACH_PTR(tmp);
}

static void match_assign(struct expression *expr)
{
	struct expression *left;
	struct symbol *sym;
	char *name;
	
	left = strip_expr(expr->left);
	name = get_variable_from_expr(left, &sym);
	if (!name)
		return;
	set_state(name, my_id, sym, alloc_extra_state(get_value(expr->right)));
	free_string(name);
}

static void undef_expr(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	
	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		return;
	if (!get_state(name, my_id, sym)) {
		free_string(name);
		return;
	}
	set_state(name, my_id, sym, &extra_undefined);
	free_string(name);
}

static void match_declarations(struct symbol *sym)
{
	const char *name;

	if (sym->ident) {
		name = sym->ident->name;
		if (sym->initializer) {
			set_state(name, my_id, sym, alloc_extra_state(get_value(sym->initializer)));
		} else {
			set_state(name, my_id, sym, &extra_undefined);
		}
	}
}

static void match_function_def(struct symbol *sym)
{
	struct symbol *arg;

	FOR_EACH_PTR(sym->ctype.base_type->arguments, arg) {
		if (!arg->ident) {
			continue;
		}
		set_state(arg->ident->name, my_id, arg, &extra_undefined);
	} END_FOR_EACH_PTR(arg);
}

static void match_unop(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	const char *tmp;
	

	name = get_variable_from_expr(expr->unop, &sym);
	if (!name)
		return;

	tmp = show_special(expr->op);
	if ((!strcmp(tmp, "--")) || (!strcmp(tmp, "++")))
		set_state(name, my_id, sym, &extra_undefined);
	free_string(name);
}

static int expr_to_val(struct expression *expr)
{
	struct smatch_state *state;
	int val;
	struct symbol *sym;
	char *name;
	
	val = get_value(expr);
	if (val != UNDEFINED)
		return val;

	name = get_variable_from_expr(expr, &sym);
	if (!name)
		return UNDEFINED;
	state = get_state(name, my_id, sym);
	free_string(name);
	if (!state || !state->data)
		return UNDEFINED;
	return get_single_value((struct data_info *)state->data);
}

int true_comparison(int left, int comparison, int right)
{
	switch(comparison){
	case '<':
	case SPECIAL_UNSIGNED_LT:
		if (left < right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_LTE:
	case SPECIAL_LTE:
		if (left < right)
			return 1;
	case SPECIAL_EQUAL:
		if (left == right)
			return 1;
		return 0;
	case SPECIAL_UNSIGNED_GTE:
	case SPECIAL_GTE:
		if (left == right)
			return 1;
	case '>':
	case SPECIAL_UNSIGNED_GT:
		if (left > right)
			return 1;
		return 0;
	case SPECIAL_NOTEQUAL:
		if (left != right)
			return 1;
		return 0;
	default:
		smatch_msg("unhandled comparison %d\n", comparison);
		return UNDEFINED;
	}
	return 0;
}

static int do_comparison(struct expression *expr)
{
	int left, right, ret;

	if ((left = expr_to_val(expr->left)) == UNDEFINED)
		return UNDEFINED;

	if ((right = expr_to_val(expr->right)) == UNDEFINED)
		return UNDEFINED;
	
	ret = true_comparison(left, expr->op, right);
	if (ret == 1) {
		SM_DEBUG("%d known condition: %d %s %d => true\n",
			get_lineno(), left, show_special(expr->op), right);
	} else if (ret == 0) {
		SM_DEBUG("%d known condition: %d %s %d => false\n",
			get_lineno(), left, show_special(expr->op), right);
	}
	return ret;
}

int last_stmt_val(struct statement *stmt)
{
	struct expression *expr;

	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt->type != STMT_EXPRESSION)
		return UNDEFINED;
	expr = stmt->expression;
	return get_value(expr);
}

static int variable_non_zero(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	struct smatch_state *state;
	int ret = UNDEFINED;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto exit;
	state = get_state(name, my_id, sym);
	if (!state || !state->data)
		goto exit;
	ret = true_comparison(get_single_value((struct data_info *)state->data),
			      SPECIAL_NOTEQUAL, 0);
exit:
	free_string(name);
	return ret;
}

int known_condition_true(struct expression *expr)
{
	struct statement *stmt;
	int tmp;

	if (!expr)
		return 0;

	tmp = get_value(expr);
	if (tmp && tmp != UNDEFINED)
		return 1;
	
	expr = strip_expr(expr);
	switch(expr->type) {
	case EXPR_COMPARE:
		if (do_comparison(expr) == 1)
			return 1;
		break;
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_false(expr->unop))
				return 1;
			break;
		}
		stmt = get_block_thing(expr);
		if (stmt && (last_stmt_val(stmt) == 1))
			return 1;
		break;
	default:
		if (variable_non_zero(expr) == 1)
			return 1;
		break;
	}
	return 0;
}

int known_condition_false(struct expression *expr)
{
	struct statement *stmt;
	struct expression *tmp;

	if (!expr)
		return 0;

	if (is_zero(expr))
		return 1;

	switch(expr->type) {
	case EXPR_COMPARE:
		if (do_comparison(expr) == 0)
			return 1;
	case EXPR_PREOP:
		if (expr->op == '!') {
			if (known_condition_true(expr->unop))
				return 1;
			break;
		}
		stmt = get_block_thing(expr);
		if (stmt && (last_stmt_val(stmt) == 0))
			return 1;
		tmp = strip_expr(expr);
		if (tmp != expr)
			return known_condition_false(tmp);
		break;
	default:
		if (variable_non_zero(expr) == 0)
			return 1;
		break;
	}
	return 0;
}

void register_smatch_extra(int id)
{
	my_id = id;
	add_merge_hook(my_id, &merge_func);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_hook(&undef_expr, OP_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_function_call, FUNCTION_CALL_HOOK);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_declarations, DECLARATION_HOOK);
	add_hook(&match_unop, OP_HOOK);
	add_hook(&free_data_info_allocs, END_FUNC_HOOK);

#ifdef KERNEL
	/* I don't know how to test for the ATTRIB_NORET attribute. :( */
	add_function_hook("panic", &__match_nullify_path_hook, NULL);
	add_function_hook("do_exit", &__match_nullify_path_hook, NULL);
	add_function_hook("complete_and_exit", &__match_nullify_path_hook, NULL);
	add_function_hook("do_group_exit", &__match_nullify_path_hook, NULL);
#endif
}
