/*
 * smatch/smatch_clear_structs.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

STATE(uninitialized);
STATE(initialized);

static int type_sym_array(struct expression *expr)
{
	struct symbol *type;

	/* remove casting the array to a pointer */
	expr = strip_expr(expr);
	type = get_type(expr);
	if (type && type->type == SYM_ARRAY)
		return 1;
	return 0;
}

static void set_members_uninitialized(struct expression *expr)
{
	struct symbol *type, *sym, *tmp;
	char *name;
	char buf[256];

	type = get_type(expr);
	if (!type)
		return;

	if (type->type != SYM_PTR)
		return;
	type = get_real_base_type(type);

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (!tmp->ident)
			continue;
		snprintf(buf, sizeof(buf), "%s->%s", name, tmp->ident->name);
		set_state(my_id, buf, sym, &uninitialized);
	} END_FOR_EACH_PTR(tmp);

free:
	free_string(name);
}

static void initialize_struct_members(struct symbol *type, struct expression *expr, struct expression *to)
{
	struct symbol *tmp;
	struct expression *member;
	struct expression *assign;
	int op = '*';

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		expr = strip_expr(expr->unop);
		op = '.';
	}

	FOR_EACH_PTR(type->symbol_list, tmp) {
		if (!tmp->ident)
			continue;
		member = member_expression(expr, op, tmp->ident);
		if (type_sym_array(member))
			continue;

		/*
		 * FIXME: the problem here is that sometimes we have:
		 *	 memset(&my_struct, 0xff, sizeof(my_struct));
		 * and my_struct->foo is a 1 bit bitfield.  There is a check
		 * which complains that "255 doesn't fit in ->foo".
		 *
		 * For now I just have this ugly hack.  But really it's not
		 * handling the memset(..., 0xff, ...) correctly at all so one
		 * more hack barely makes a difference.
		 *
		 */
		if (to && type_positive_bits(get_type(member)) >= type_positive_bits(get_type(to)))
			assign = assign_expression(member, to);
		else
			assign = assign_expression(member, member);

		__split_expr(assign);
	} END_FOR_EACH_PTR(tmp);
}

static void initialize_base_type(struct symbol *type, struct expression *expr,
		struct expression *to)
{
	struct expression *assign;

	if (type == &void_ctype)
		return;
	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);
	else
		expr = deref_expression(expr);
	if (!to)
		to = expr;
	/* FIXME: see the FIXME in initialize_struct_members() */
	if (type_positive_bits(get_type(expr)) < type_positive_bits(get_type(to)))
		to = expr;
	assign = assign_expression(expr, to);
	__split_expr(assign);
}

void set_initialized(struct expression *expr, struct expression *to)
{
	struct symbol *type;

	expr = strip_expr(expr);

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return;
	type = get_real_base_type(type);
	if (!type)
		return;
	if (type->type == SYM_BASETYPE)
		initialize_base_type(type, expr, to);
	if (type->type == SYM_STRUCT) {
		if (expr->type != EXPR_PREOP || expr->op != '&')
			expr = deref_expression(expr);
		initialize_struct_members(type, expr, to);
	}
}

int is_uninitialized(struct expression *expr)
{
	struct sm_state *sm;

	sm = get_sm_state_expr(my_id, expr);
	if (!sm)
		return 0;
	return slist_has_state(sm->possible, &uninitialized);
}

int has_uninitialized_members(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *tmp;
	char *name;
	char buf[256];
	struct sm_state *sm;

	sym = get_type(expr);
	if (!sym)
		return 0;

	if (sym->type == SYM_PTR)
		sym = get_real_base_type(sym);

	name = expr_to_var(expr);
	if (!name)
		return 0;

	FOR_EACH_PTR(sym->symbol_list, tmp) {
		if (!tmp->ident)
			continue;
		snprintf(buf, sizeof(buf), "%s->%s", name, tmp->ident->name);
		sm = get_sm_state(my_id, buf, sym);
		if (!sm)
			continue;
		if (slist_has_state(sm->possible, &uninitialized))
			return 1;
	} END_FOR_EACH_PTR(tmp);

	free_string(name);
	return 0;
}

static void match_assign(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr->left);
	if (!type || type->type != SYM_STRUCT)
		return;
	initialize_struct_members(type, expr->left, NULL);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	set_members_uninitialized(expr->left);
}

static void db_param_cleared(struct expression *expr, int param, char *key, char *value)
{
	struct expression *arg;

	while (expr->type == EXPR_ASSIGNMENT)
		expr = strip_expr(expr->right);
	if (expr->type != EXPR_CALL)
		return;

	arg = get_argument_from_call_expr(expr->args, param);
	if (!arg)
		return;

	if (strcmp(value, "0") == 0)
		set_initialized(arg, zero_expr());
	else
		set_initialized(arg, NULL);
}

static void match_memset(const char *fn, struct expression *expr, void *_size_arg)
{
	struct expression *buf;
	struct expression *val;

	buf = get_argument_from_call_expr(expr->args, 0);
	val = get_argument_from_call_expr(expr->args, 1);

	set_initialized(buf, val);
}

static void match_memcpy(const char *fn, struct expression *expr, void *_arg)
{
	struct expression *buf;
	int arg = PTR_INT(_arg);

	buf = get_argument_from_call_expr(expr->args, arg);

	set_initialized(buf, NULL);
}

static void reset_initialized(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &initialized);
}

#define USB_DIR_IN 0x80
static void match_usb_control_msg(const char *fn, struct expression *expr, void *_size_arg)
{
	struct expression *inout;
	struct expression *buf;
	sval_t sval;

	inout = get_argument_from_call_expr(expr->args, 3);
	buf = get_argument_from_call_expr(expr->args, 6);

	if (get_value(inout, &sval) && !(sval.uvalue & USB_DIR_IN))
		return;

	set_initialized(buf, NULL);
}

static void register_clears_param(void)
{
	struct token *token;
	char name[256];
	const char *function;
	int param;

	if (option_project == PROJ_NONE)
		return;

	snprintf(name, 256, "%s.clears_argument", option_project_str);

	token = get_tokens_file(name);
	if (!token)
		return;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return;
	token = token->next;
	while (token_type(token) != TOKEN_STREAMEND) {
		if (token_type(token) != TOKEN_IDENT)
			return;
		function = show_ident(token->ident);
		token = token->next;
		if (token_type(token) != TOKEN_NUMBER)
			return;
		param = atoi(token->number);
		add_function_hook(function, &match_memcpy, INT_PTR(param));
		token = token->next;
	}
	clear_token_alloc();
}

void register_clear_buffer(int id)
{
	my_id = id;
	if (option_project == PROJ_KERNEL) {
		add_function_assign_hook("kmalloc", &match_alloc, NULL);
		add_function_hook("usb_control_msg", &match_usb_control_msg, NULL);
	}
	add_modification_hook(my_id, &reset_initialized);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_function_hook("memset", &match_memset, NULL);
	add_function_hook("memcpy", &match_memcpy, INT_PTR(0));
	add_function_hook("memmove", &match_memcpy, INT_PTR(0));
	register_clears_param();

	add_db_return_states_callback(PARAM_CLEARED, &db_param_cleared);
}

