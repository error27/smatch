/*
 * sparse/check_deference.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include "parse.h"
#include "smatch.h"
#include "smatch_slist.h"

struct bound {
	int param;
	int size;
};

static int my_id;

static struct symbol *this_func;

static void match_function_def(struct symbol *sym)
{
	this_func = sym;
}

static void print_args(struct expression *expr, int size)
{
	struct symbol *sym;
	char *name;
	struct symbol *arg;
	const char *arg_name;
	int i;

	if (!option_spammy)
		return;

	name = get_variable_from_expr(expr, &sym);
	if (!name || !sym)
		goto free;

	i = 0;
	FOR_EACH_PTR(this_func->ctype.base_type->arguments, arg) {
		arg_name = (arg->ident?arg->ident->name:"-");
		if (sym == arg && !strcmp(name, arg_name)) {
			sm_msg("param %d array index. size %d", i, size);
			goto free;
		}
		i++;
	} END_FOR_EACH_PTR(arg);
free:
	free_string(name);
}

static char *alloc_num(long long num)
{
	static char buff[256];

	if (num == whole_range.min) {
		snprintf(buff, 255, "min");
	} else if (num == whole_range.max) {
		snprintf(buff, 255, "max");
	} else if (num < 0) {
		snprintf(buff, 255, "(%lld)", num);
	} else {
		snprintf(buff, 255, "%lld", num);
	}
	buff[255] = '\0';
	return alloc_sname(buff);
}

static struct smatch_state *alloc_my_state(int val)
{
	struct smatch_state *state;

	state = malloc(sizeof(*state));
	state->name = alloc_num(val);
	state->data = malloc(sizeof(int));
	*(int *)state->data = val;
	return state;
}

static void match_declaration(struct symbol *sym)
{
	struct symbol *base_type;
	char *name;
	int size;

	if (!sym->ident)
		return;

	name = sym->ident->name;
	base_type = get_base_type(sym);
	
	if (base_type->type == SYM_ARRAY && base_type->bit_size > 0) {
		set_state(my_id, name, NULL, alloc_my_state(base_type->bit_size));
	} else {
		if (sym->initializer &&
 			sym->initializer->type == EXPR_STRING &&
			sym->initializer->string) {
			size = sym->initializer->string->length * 8;
			set_state(my_id, name, NULL, alloc_my_state(size));
		}
	}
}

static int get_array_size(struct expression *expr)
{
	char *name;
	struct symbol *tmp;
	struct smatch_state *state;
	int ret = 0;

	if (expr->type != EXPR_SYMBOL)
		return 0;
	tmp = get_base_type(expr->symbol);
	if (tmp->type == SYM_ARRAY) {
		ret = get_expression_value(tmp->array_size);
		if (ret)
			return ret;
	}
	name = get_variable_from_expr(expr, NULL);
	if (!name)
		return 0;
	state = get_state(my_id, name, NULL);
	if (!state || !state->data)
		goto free;
	if (tmp->type == SYM_PTR)
		tmp = get_base_type(tmp);
	ret = *(int *)state->data / 8 / tmp->ctype.alignment;
free:
	free_string(name);
	return ret;
}

extern int check_assigned_expr_id;
static void print_assigned_expr(struct expression *expr)
{
#if 0
	struct state_list *slist;
	struct sm_state *tmp;
	char *name;

	name = get_variable_from_expr(expr, NULL);
	slist = get_possible_states_expr(check_assigned_expr_id, expr);
	FOR_EACH_PTR(slist, tmp) {
		if (tmp->state == &undefined || tmp->state == &merged)
			continue;
		smatch_msg("debug: unknown initializer %s = %s", name, show_state(tmp->state));
	} END_FOR_EACH_PTR(tmp);
	free_string(name);
#endif
}

static void array_check(struct expression *expr)
{
	struct expression *dest;
	int array_size;
	struct expression *offset;
	long long max;
	char *name;

	expr = strip_expr(expr);
	if (!is_array(expr))
		return;

	dest = get_array_name(expr);
	array_size = get_array_size(dest);
	if (!array_size) {
		name = get_variable_from_expr(dest, NULL);
		if (!name)
			return;
//		smatch_msg("debug: array '%s' unknown size", name);
		print_assigned_expr(dest);
		return;
	}

	offset = get_array_offset(expr);
	if (!get_implied_max(offset, &max)) {
		name = get_variable_from_expr(dest, NULL);
//		smatch_msg("debug: offset '%s' unknown", name);
		print_args(offset, array_size);
	} else if (array_size <= max) {
		name = get_variable_from_expr(dest, NULL);
		/*FIXME!!!!!!!!!!!
		  blast.  smatch can't figure out glibc's strcmp __strcmp_cg()
		  so it prints an error every time you compare to a string
		  literal array with 4 or less chars. */
		if (strcmp(name, "__s1") && strcmp(name, "__s2"))
			sm_msg("error: buffer overflow '%s' %d <= %lld", name, array_size, max);
		free_string(name);
	}
}

static void match_string_assignment(struct expression *expr)
{
	struct expression *left;
	struct expression *right;
	char *name;

	left = strip_expr(expr->left);
	right = strip_expr(expr->right);
	name = get_variable_from_expr(left, NULL);
	if (!name)
		return;
	if (right->type != EXPR_STRING || !right->string)
		goto free;
	set_state(my_id, name, NULL, 
		alloc_my_state(right->string->length * 8));
free:
	free_string(name);
}

static void match_malloc(const char *fn, struct expression *expr, void *unused)
{
	char *name;
	struct expression *right;
	struct expression *arg;
	long long bytes;

	name = get_variable_from_expr(expr->left, NULL);
	if (!name)
		return;

	right = strip_expr(expr->right);
	arg = get_argument_from_call_expr(right->args, 0);
	if (!get_implied_value(arg, &bytes))
		goto free;
	set_state(my_id, name, NULL, alloc_my_state(bytes * 8));
free:
	free_string(name);
}

static void match_strcpy(const char *fn, struct expression *expr,
			 void *unused)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	char *data_name = NULL;
	struct smatch_state *dest_state;
	struct smatch_state *data_state;
	int dest_size;
	int data_size;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, 1);
	data_name = get_variable_from_expr(data, NULL);
		
	dest_state = get_state(my_id, dest_name, NULL);
	if (!dest_state || !dest_state->data)
		goto free;

	data_state = get_state(my_id, data_name, NULL);
	if (!data_state || !data_state->data)
		goto free;
	dest_size = *(int *)dest_state->data / 8;
	data_size = *(int *)data_state->data / 8;
	if (dest_size < data_size)
		sm_msg("error: %s (%d) too large for %s (%d)", data_name,
			   data_size, dest_name, dest_size);
free:
	free_string(dest_name);
	free_string(data_name);
}

static void match_limitted(const char *fn, struct expression *expr,
			   void *limit_arg)
{
	struct expression *dest;
	struct expression *data;
	char *dest_name = NULL;
	struct smatch_state *state;
	long long needed;
	int has;

	dest = get_argument_from_call_expr(expr->args, 0);
	dest_name = get_variable_from_expr(dest, NULL);

	data = get_argument_from_call_expr(expr->args, PTR_INT(limit_arg));
	if (!get_value(data, &needed))
		goto free;
	state = get_state(my_id, dest_name, NULL);
	if (!state || !state->data)
		goto free;
	has = *(int *)state->data / 8;
	if (has < needed)
		sm_msg("error: %s too small for %lld bytes.", dest_name,
			   needed);
free:
	free_string(dest_name);
}

static void match_array_func(const char *fn, struct expression *expr, void *info)
{
	struct bound *bound_info = (struct bound *)info;
	struct expression *arg;
	long long offset;

	arg = get_argument_from_call_expr(expr->args, bound_info->param);
	if (!get_implied_value(arg, &offset))
		return;
	if (offset >= bound_info->size)
		sm_msg("buffer overflow calling %s. param %d.  %lld >= %d", fn, bound_info->param, offset, bound_info->size);
}

static void register_array_funcs(void)
{
	struct token *token;
	const char *func;
	struct bound *bound_info;

	token = get_tokens_file("kernel.array_bounds");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		bound_info = malloc(sizeof(*bound_info));
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		bound_info->param = atoi(token->number);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		bound_info->size = atoi(token->number);
		add_function_hook(func, &match_array_func, bound_info);
		token = token->next;
	}
	clear_token_alloc();
}

void check_overflow(int id)
{
	my_id = id;
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declaration, DECLARATION_HOOK);
	add_hook(&array_check, OP_HOOK);
	add_hook(&match_string_assignment, ASSIGNMENT_HOOK);
	add_function_assign_hook("malloc", &match_malloc, NULL);
	add_function_hook("strcpy", &match_strcpy, NULL);
	add_function_hook("strncpy", &match_limitted, (void *)2);
	if (option_project == PROJ_KERNEL) {
		add_function_assign_hook("kmalloc", &match_malloc, NULL);
		add_function_assign_hook("kzalloc", &match_malloc, NULL);
		add_function_assign_hook("vmalloc", &match_malloc, NULL);
		add_function_hook("copy_to_user", &match_limitted, (void *)2);
		add_function_hook("copy_from_user", &match_limitted, (void *)2);
	}
	register_array_funcs();
}
