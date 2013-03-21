/*
 * sparse/smatch_helper.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Miscellaneous helper functions.
 */

#include <stdlib.h>
#include <stdio.h>
#include "allocate.h"
#include "smatch.h"

#define VAR_LEN 512

char *alloc_string(const char *str)
{
	char *tmp;

	if (!str)
		return NULL;
	tmp = malloc(strlen(str) + 1);
	strcpy(tmp, str);
	return tmp;
}

void free_string(char *str)
{
	free(str);
}

void remove_parens(char *str)
{
	char *src, *dst;

	dst = src = str;
	while (*src != '\0') {
		if (*src == '(' || *src == ')') {
			src++;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = *src;
}

struct smatch_state *alloc_state_num(int num)
{
	struct smatch_state *state;
	static char buff[256];

	state = __alloc_smatch_state(0);
	snprintf(buff, 255, "%d", num);
	buff[255] = '\0';
	state->name = alloc_string(buff);
	state->data = INT_PTR(num);
	return state;
}

static void append(char *dest, const char *data, int buff_len)
{
	strncat(dest, data, buff_len - strlen(dest) - 1);
}

/*
 * If you have "foo(a, b, 1);" then use
 * get_argument_from_call_expr(expr, 0) to return the expression for
 * a.  Yes, it does start counting from 0.
 */
struct expression *get_argument_from_call_expr(struct expression_list *args,
					       int num)
{
	struct expression *expr;
	int i = 0;

	if (!args)
		return NULL;

	FOR_EACH_PTR(args, expr) {
		if (i == num)
			return expr;
		i++;
	} END_FOR_EACH_PTR(expr);
	return NULL;
}

static struct expression *get_array_expr(struct expression *expr)
{
	struct symbol *type;

	if (expr->type != EXPR_BINOP || expr->op != '+')
		return NULL;

	type = get_type(expr->left);
	if (!type || type->type != SYM_ARRAY)
		return NULL;
	return expr->left;
}

static void __get_variable_from_expr(struct symbol **sym_ptr, char *buf,
				     struct expression *expr, int len,
				     int *complicated)
{
	struct expression *tmp;

	switch (expr->type) {
	case EXPR_DEREF:
		tmp = expr->deref;
		if (tmp->op == '*')
			tmp = tmp->unop;

		__get_variable_from_expr(sym_ptr, buf, tmp, len, complicated);

		tmp = expr->deref;
		if (tmp->op == '*')
			append(buf, "->", len);
		else
			append(buf, ".", len);

		append(buf, expr->member->name, len);

		return;
	case EXPR_SYMBOL:
		if (expr->symbol_name)
			append(buf, expr->symbol_name->name, len);
		if (sym_ptr) {
			if (*sym_ptr)
				*complicated = 1;
			*sym_ptr = expr->symbol;
		}
		return;
	case EXPR_PREOP: {
		const char *tmp;

		if (get_expression_statement(expr)) {
			*complicated = 2;
			return;
		}

		if (expr->op != '*' || !get_array_expr(expr->unop)) {
			tmp = show_special(expr->op);
			append(buf, tmp, len);
		}
		__get_variable_from_expr(sym_ptr, buf, expr->unop,
						 len, complicated);

		if (expr->op == '(')
			append(buf, ")", len);

		if (expr->op == SPECIAL_DECREMENT ||
				expr->op == SPECIAL_INCREMENT ||
				expr->op == '&')
			*complicated = 1;

		return;
	}
	case EXPR_POSTOP: {
		const char *tmp;

		__get_variable_from_expr(sym_ptr, buf, expr->unop,
						 len, complicated);
		tmp = show_special(expr->op);
		append(buf, tmp, len);

		if (expr->op == SPECIAL_DECREMENT || expr->op == SPECIAL_INCREMENT)
			*complicated = 1;
		return;
	}
	case EXPR_ASSIGNMENT:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_BINOP: {
		char tmp[10];
		struct expression *array_expr;

		*complicated = 1;
		array_expr = get_array_expr(expr);
		if (array_expr) {
			__get_variable_from_expr(sym_ptr, buf, array_expr, len, complicated);
			append(buf, "[", len);
		} else {
			__get_variable_from_expr(sym_ptr, buf, expr->left, len,
					 complicated);
			snprintf(tmp, sizeof(tmp), " %s ", show_special(expr->op));
			append(buf, tmp, len);
		}
		__get_variable_from_expr(NULL, buf, expr->right,
						 len, complicated);
		if (array_expr)
			append(buf, "]", len);
		return;
	}
	case EXPR_VALUE: {
		char tmp[25];

		*complicated = 1;
		snprintf(tmp, 25, "%lld", expr->value);
		append(buf, tmp, len);
		return;
	}
	case EXPR_STRING:
		append(buf, "\"", len);
		append(buf, expr->string->data, len);
		append(buf, "\"", len);
		return;
	case EXPR_CALL: {
		struct expression *tmp;
		int i;

		*complicated = 1;
		__get_variable_from_expr(NULL, buf, expr->fn, len,
					 complicated);
		append(buf, "(", len);
		i = 0;
		FOR_EACH_PTR(expr->args, tmp) {
			if (i++)
				append(buf, ", ", len);
			__get_variable_from_expr(NULL, buf, tmp, len,
						complicated);
		} END_FOR_EACH_PTR(tmp);
		append(buf, ")", len);
		return;
	}
	case EXPR_CAST:
		__get_variable_from_expr(sym_ptr, buf,
					 expr->cast_expression, len,
					 complicated);
		return;
	case EXPR_SIZEOF: {
		int size;
		char tmp[25];

		if (expr->cast_type && get_base_type(expr->cast_type)) {
			size = (get_base_type(expr->cast_type))->bit_size;
			snprintf(tmp, 25, "%d", bits_to_bytes(size));
			append(buf, tmp, len);
		}
		return;
	}
	default:
		*complicated = 1;
		//printf("unknown type = %d\n", expr->type);
		return;
	}
}

/*
 * This is returns a stylized "c looking" representation of the
 * variable name.
 *
 * It uses the same buffer every time so you have to save the result
 * yourself if you want to keep it.
 *
 */

char *expr_to_str_sym(struct expression *expr, struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int complicated = 0;

	if (sym_ptr)
		*sym_ptr = NULL;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &complicated);
	if (complicated < 2)
		return alloc_string(var_name);
	else
		return NULL;
}

char *expr_to_str(struct expression *expr)
{
	return expr_to_str_sym(expr, NULL);
}

/*
 * get_variable_from_expr_simple() only returns simple variables.
 * If it's a complicated variable like a->foo[x] instead of just 'a->foo'
 * then it returns NULL.
 */

char *expr_to_var_sym(struct expression *expr,
				    struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int complicated = 0;

	if (sym_ptr)
		*sym_ptr = NULL;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	expr = strip_expr(expr);
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &complicated);

	if (complicated) {
		if (sym_ptr)
			*sym_ptr = NULL;
		return NULL;
	}
	return alloc_string(var_name);
}

char *expr_to_var(struct expression *expr)
{
	return expr_to_var_sym(expr, NULL);
}

int sym_name_is(const char *name, struct expression *expr)
{
	if (!expr)
		return 0;
	if (expr->type != EXPR_SYMBOL)
		return 0;
	if (!strcmp(expr->symbol_name->name, name))
		return 1;
	return 0;
}

int is_zero(struct expression *expr)
{
	sval_t sval;

	if (get_value(expr, &sval) && sval.value == 0)
		return 1;
	return 0;
}

int is_array(struct expression *expr)
{
	expr = strip_expr(expr);
	if (expr->type != EXPR_PREOP || expr->op != '*')
		return 0;
	expr = expr->unop;
	if (expr->op != '+')
		return 0;
	return 1;
}

struct expression *get_array_name(struct expression *expr)
{
	if (!is_array(expr))
		return NULL;
	return strip_expr(expr->unop->left);
}

struct expression *get_array_offset(struct expression *expr)
{
	if (!is_array(expr))
		return NULL;
	return expr->unop->right;
}

const char *show_state(struct smatch_state *state)
{
	if (!state)
		return NULL;
	return state->name;
}

struct statement *get_expression_statement(struct expression *expr)
{
	/* What are those things called? if (({....; ret;})) { ...*/

	if (expr->type != EXPR_PREOP)
		return NULL;
	if (expr->op != '(')
		return NULL;
	if (expr->unop->type != EXPR_STATEMENT)
		return NULL;
	if (expr->unop->statement->type != STMT_COMPOUND)
		return NULL;
	return expr->unop->statement;
}

struct expression *strip_parens(struct expression *expr)
{
	if (!expr)
		return NULL;

	if (expr->type == EXPR_PREOP) {
		if (expr->op == '(' && expr->unop->type == EXPR_STATEMENT &&
			expr->unop->statement->type == STMT_COMPOUND)
			return expr;
		if (expr->op == '(')
			return strip_parens(expr->unop);
	}
	return expr;
}

struct expression *strip_expr(struct expression *expr)
{
	if (!expr)
		return NULL;

	switch (expr->type) {
	case EXPR_FORCE_CAST:
	case EXPR_CAST:
		return strip_expr(expr->cast_expression);
	case EXPR_PREOP:
		if (expr->op == '(' && expr->unop->type == EXPR_STATEMENT &&
			expr->unop->statement->type == STMT_COMPOUND)
			return expr;
		if (expr->op == '(')
			return strip_expr(expr->unop);
	}
	return expr;
}

static void delete_state_tracker(struct tracker *t)
{
	delete_state(t->owner, t->name, t->sym);
	__free_tracker(t);
}

void scoped_state(int my_id, const char *name, struct symbol *sym)
{
	struct tracker *t;

	t = alloc_tracker(my_id, name, sym);
	add_scope_hook((scope_hook *)&delete_state_tracker, t);
}

int is_error_return(struct expression *expr)
{
	struct symbol *cur_func = cur_func_sym;
	sval_t sval;

	if (!expr)
		return 0;
	if (cur_func->type != SYM_NODE)
		return 0;
	cur_func = get_base_type(cur_func);
	if (cur_func->type != SYM_FN)
		return 0;
	cur_func = get_base_type(cur_func);
	if (cur_func == &void_ctype)
		return 0;
	if (!get_value(expr, &sval))
		return 0;
	if (sval_cmp_val(sval, 0) < 0)
		return 1;
	if (cur_func->type == SYM_PTR && sval.value == 0)
		return 1;
	return 0;
}

int getting_address(void)
{
	struct expression *tmp;
	int i = 0;
	int dot_ops = 0;

	FOR_EACH_PTR_REVERSE(big_expression_stack, tmp) {
		if (!i++)
			continue;
		if (tmp->type == EXPR_PREOP && tmp->op == '(')
			continue;
		if (tmp->op == '.' && !dot_ops++)
			continue;
		if (tmp->op == '&')
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return 0;
}

char *get_member_name(struct expression *expr)
{
	char buf[256];
	struct symbol *sym;

	expr = strip_expr(expr);
	if (expr->type != EXPR_DEREF)
		return NULL;
	sym = get_type(expr->deref);
	if (!sym || !sym->ident)
		return NULL;
	snprintf(buf, sizeof(buf), "(struct %s)->%s", sym->ident->name, expr->member->name);
	return alloc_string(buf);
}

int positions_eq(struct position pos1, struct position pos2)
{
	if (pos1.line != pos2.line)
		return 0;
	if (pos1.pos != pos2.pos)
		return 0;
	if (pos1.stream != pos2.stream)
		return 0;
	return 1;
}

struct statement *get_current_statement(void)
{
	struct statement *prev, *tmp;

	prev = last_ptr_list((struct ptr_list *)big_statement_stack);

	if (!prev || !get_macro_name(prev->pos))
		return prev;

	FOR_EACH_PTR_REVERSE(big_statement_stack, tmp) {
		if (positions_eq(tmp->pos, prev->pos))
			continue;
		if (prev->pos.line > tmp->pos.line)
			return prev;
		return tmp;
	} END_FOR_EACH_PTR_REVERSE(tmp);
	return prev;
}

int get_param_num_from_sym(struct symbol *sym)
{
	struct symbol *tmp;
	int i;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, tmp) {
		if (tmp == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(tmp);
	return -1;
}

