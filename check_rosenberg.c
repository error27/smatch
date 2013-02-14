/*
 * sparse/check_rosenberg.c
 *
 * Copyright (C) 2011 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/* Does a search for Dan Rosenberg style info leaks */

/* fixme: struct includes a struct with a hole in it */
/* function is called that clears the struct */

#include "scope.h"
#include "smatch.h"
#include "smatch_function_hashtable.h"
#include "smatch_slist.h"

static int my_id;
extern int check_assigned_expr_id;

STATE(cleared);

static DEFINE_HASHTABLE_INSERT(insert_struct, char, int);
static DEFINE_HASHTABLE_SEARCH(search_struct, char, int);
static struct hashtable *holey_structs;

static char *get_struct_type(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type || type->type != SYM_STRUCT || !type->ident)
		return NULL;
	return alloc_string(type->ident->name);
}

static int holey_struct(struct expression *expr)
{
	char *struct_type = 0;
	int ret = 0;

	struct_type = get_struct_type(expr);
	if (!struct_type)
		return 0;
	if (search_struct(holey_structs, struct_type))
		ret = 1;
	free_string(struct_type);
	return ret;
}

static int has_global_scope(struct expression *expr)
{
	struct symbol *sym;

	if (expr->type != EXPR_SYMBOL)
		return FALSE;
	sym = expr->symbol;
	return toplevel(sym->scope);
}

static int was_initialized(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return 0;
	if (sym->initializer)
		return 1;
	return 0;
}

static void match_clear(const char *fn, struct expression *expr, void *_arg_no)
{
	struct expression *ptr;
	int arg_no = PTR_INT(_arg_no);

	ptr = get_argument_from_call_expr(expr->args, arg_no);
	if (!ptr)
		return;
	if (ptr->type != EXPR_PREOP || ptr->op != '&')
		return;
	ptr = strip_expr(ptr->unop);
	set_state_expr(my_id, ptr, &cleared);
}

static int was_memset(struct expression *expr)
{
	if (get_state_expr(my_id, expr) == &cleared)
		return 1;
	return 0;
}

static int member_initialized(char *name, struct symbol *outer, struct symbol *member)
{
	char buf[256];
	struct symbol *base;

	base = get_base_type(member);
	if (!base || base->type != SYM_BASETYPE || !member->ident)
		return FALSE;

	snprintf(buf, 256, "%s.%s", name, member->ident->name);
	if (get_state(check_assigned_expr_id, buf, outer))
		return TRUE;

	return FALSE;
}

static int member_uninitialized(char *name, struct symbol *outer, struct symbol *member)
{
	char buf[256];
	struct symbol *base;
	struct sm_state *sm;

	base = get_base_type(member);
	if (!base || base->type != SYM_BASETYPE || !member->ident)
		return FALSE;

	snprintf(buf, 256, "%s.%s", name, member->ident->name);
	sm = get_sm_state(check_assigned_expr_id, buf, outer);
	if (sm && !slist_has_state(sm->possible, &undefined))
		return FALSE;

	sm_msg("warn: check that '%s' doesn't leak information", buf);
	return TRUE;
}

static void check_members_initialized(struct expression *expr)
{
	char *name;
	struct symbol *outer;
	struct symbol *sym;
	struct symbol *tmp;

	sym = get_type(expr);
	if (!sym || sym->type != SYM_STRUCT)
		return;

	name = expr_to_var_sym(expr, &outer);

	if (get_state(check_assigned_expr_id, name, outer))
		goto out;

	FOR_EACH_PTR(sym->symbol_list, tmp) {
		if (member_initialized(name, outer, tmp))
			goto check;
	} END_FOR_EACH_PTR(tmp);
	goto out;

check:
	FOR_EACH_PTR(sym->symbol_list, tmp) {
		if (member_uninitialized(name, outer, tmp))
			goto out;
	} END_FOR_EACH_PTR(tmp);
out:
	free_string(name);
}

static void match_copy_to_user(const char *fn, struct expression *expr, void *unused)
{
	struct expression *data;

	data = get_argument_from_call_expr(expr->args, 1);
	if (!data)
		return;
	if (data->type != EXPR_PREOP || data->op != '&')
		return;

	data = strip_expr(data->unop);
	if (data->type != EXPR_SYMBOL)
		return;

	if (has_global_scope(data))
		return;
	if (was_initialized(data))
		return;
	if (was_memset(data))
		return;
	if (holey_struct(data)) {
		char *name;

		name = expr_to_var(data);
		sm_msg("warn: check that '%s' doesn't leak information (struct has holes)", name);
		free_string(name);
		return;
	}
	check_members_initialized(data);
}

static void register_holey_structs(void)
{
	struct token *token;
	const char *struct_type;

	token = get_tokens_file("kernel.paholes");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		struct_type = show_ident(token->ident);
		insert_struct(holey_structs, alloc_string(struct_type), INT_PTR(1));
		token = token->next;
	}
	clear_token_alloc();
}

static void register_clears_argument(void)
{
	struct token *token;
	const char *func;
	int arg;

	token = get_tokens_file("kernel.clears_argument");
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		func = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		arg = atoi(token->number);

		add_function_hook(func, &match_clear, INT_PTR(arg));
		token = token->next;
	}
	clear_token_alloc();
}

void check_rosenberg(int id)
{
	if (option_project != PROJ_KERNEL)
		return;
	my_id = id;
	holey_structs = create_function_hashtable(10000);

	register_holey_structs();
	register_clears_argument();

	add_function_hook("copy_to_user", &match_copy_to_user, NULL);
}
