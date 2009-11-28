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
#define BOGUS 12345

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

static void __get_variable_from_expr(struct symbol **sym_ptr, char *buf, 
				     struct expression *expr, int len,
				     int *complicated)
{
	struct expression *tmp;

	switch(expr->type) {
	case EXPR_DEREF:
		tmp = expr->deref;
		if (tmp->op == '*')  {
			tmp = tmp->unop;
		}
		__get_variable_from_expr(sym_ptr, buf, tmp, len, complicated);

		tmp = expr->deref;
		if (tmp->op == '*')  {
			append(buf, "->", len);
		} else {
			append(buf, ".", len);
		}		
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

		if (get_block_thing(expr)) {
			*complicated = 2;
			return;
		}

		tmp = show_special(expr->op);
		append(buf, tmp, len);
		__get_variable_from_expr(sym_ptr, buf, expr->unop, 
						 len, complicated);

		if (expr->op == '(') {
			append(buf, ")", len);
		}

		if (expr->op == SPECIAL_DECREMENT || expr->op == SPECIAL_INCREMENT)
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
	case EXPR_BINOP: {
		const char *tmp;

		*complicated = 1;
		append(buf, "(", len);
		__get_variable_from_expr(NULL, buf, expr->left, len,
					 complicated);
		tmp = show_special(expr->op);
		append(buf, tmp, len);
		__get_variable_from_expr(sym_ptr, buf, expr->right, 
						 len, complicated);
		append(buf, ")", len);
		return;
	}
	case EXPR_VALUE: {
		char tmp[25];

		snprintf(tmp, 25, "%lld", expr->value);
		append(buf, tmp, len);
		return;
	}
	case EXPR_STRING:
		append(buf, expr->string->data, len);
		return;
	case EXPR_CALL: {
		struct expression *tmp;
		int i;
		
		*complicated = 1;
		__get_variable_from_expr(NULL, buf, expr->fn, len,
					 complicated);
		append(buf, "(", len);
		i = 0;
		FOR_EACH_PTR_REVERSE(expr->args, tmp) {
			if (i++)
				append(buf, ", ", len);
			__get_variable_from_expr(NULL, buf, tmp, len,
						complicated);
		} END_FOR_EACH_PTR_REVERSE(tmp);
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
			snprintf(tmp, 25, "%d", size);
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

char *get_variable_from_expr_complex(struct expression *expr, struct symbol **sym_ptr)
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

/*
 * get_variable_from_expr_simple() only returns simple variables.
 * If it's a complicated variable like a->foo instead of just 'a'
 * then it returns NULL.
 */

char *get_variable_from_expr(struct expression *expr, 
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

struct symbol *get_ptr_type_ptr(struct symbol *sym)
{
	if (!sym) {
		return NULL;
	}
	
	if (sym->type != SYM_NODE)
		return NULL;
	sym = get_base_type(sym);
	if (sym->type != SYM_PTR)
		return NULL;
	sym = get_base_type(sym);
	return sym;
}

static struct symbol *get_struct_sym(struct expression *expr)
{
	struct symbol *base_type;
	struct symbol *parent_struct;
	struct symbol *tmp;

	if (expr->type != EXPR_PREOP)
		return NULL;

	expr = expr->unop;
	if (expr->type == EXPR_DEREF) {
		parent_struct = get_struct_sym(expr->deref);
		if (!parent_struct)
			return NULL;
		tmp = NULL;
		FOR_EACH_PTR(parent_struct->symbol_list, tmp) {
			if (tmp->ident == expr->member)
				break;
		} END_FOR_EACH_PTR(tmp);
		if (!tmp || tmp->ident != expr->member)
			return NULL;
		base_type = get_base_type(tmp);
	} else if (expr->type == EXPR_SYMBOL) {
		base_type = get_base_type(expr->symbol);
	} else {
		return NULL;
	}
	if (base_type->type != SYM_PTR)
		return NULL;
	base_type = get_base_type(base_type);
	if (base_type->type != SYM_STRUCT && base_type->type != SYM_UNION)
		return NULL;
	return base_type;
}

struct symbol *get_deref_type(struct expression *expr)
{
	struct ident *member = expr->member;
	struct symbol *struct_sym;
	struct symbol *tmp;

	struct_sym = get_struct_sym(expr->deref);
	if (!struct_sym || (struct_sym->type != SYM_STRUCT 
			    && struct_sym->type != SYM_UNION))
		return NULL;
	FOR_EACH_PTR(struct_sym->symbol_list, tmp) {
		if (tmp->ident == member)
			return get_ptr_type_ptr(tmp);
	} END_FOR_EACH_PTR(tmp);
	return NULL;
}

struct symbol *get_ptr_type(struct expression *expr)
{
	struct symbol *ptr_type = NULL;

	if (!expr)
		return NULL;
	if (expr->type == EXPR_DEREF)
		ptr_type = get_deref_type(expr);
	if (expr->type == EXPR_SYMBOL)
		ptr_type = get_ptr_type_ptr(expr->symbol);
	return ptr_type;
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

#define NOTIMPLIED 0
#define IMPLIED 1

static int _get_value(struct expression *expr, int *discard, int *undefined, int implied)
{
	int dis = 0;
	long long ret = BOGUS;

	if (!expr) {
		*undefined = 1;
		return BOGUS;
	}
	if (!discard)
		discard = &dis;
	if (*discard) {
		*undefined = 1;
		return BOGUS;
	}
	
	expr = strip_expr(expr);

 	switch (expr->type){
	case EXPR_VALUE:
		ret = expr->value;
		break;
	case EXPR_PREOP:
		if (expr->op == '-') {
			ret = - _get_value(expr->unop, discard, undefined, implied);
		} else {
			*undefined = 1;
			*discard = 1;
		}
		break;
	case EXPR_BINOP: {
		int left, right;

		left = _get_value(expr->left, discard, undefined, implied);
		right = _get_value(expr->right, discard, undefined, implied);
		if (expr->op == '*') {
			ret =  left * right;
		} else if (expr->op == '/') {
			ret = left / right;
		} else if (expr->op == '+') {
			ret = left + right;
		} else if (expr->op == '-') {
			ret = left - right;
		} else if (expr->op == '|') {
			ret = left | right;
		} else if (expr->op == '&') {
			ret = left & right;
		} else if (expr->op == SPECIAL_RIGHTSHIFT) {
			ret = left >> right;
		} else if (expr->op == SPECIAL_LEFTSHIFT) {
			ret = left << right;
		} else {
			*undefined = 1;
			*discard = 1;
		}
		break;
	}
	case EXPR_PTRSIZEOF:
	case EXPR_SIZEOF:
		ret = get_expression_value(expr);
		break;
	default:
		if (implied) {
			if (!get_implied_single_val(expr, &ret)) {
				*undefined = 1;
				*discard = 1;
			}
		} else {
			*undefined = 1;
			*discard = 1;
		}
	}
	if (*discard) {
		*undefined = 1;
		return BOGUS;
	}
	return ret;
}

/* returns 1 if it can get a value literal or else returns 0 */
int get_value(struct expression *expr, long long *val)
{
	int undefined = 0;
	
	*val = _get_value(expr, NULL, &undefined, NOTIMPLIED);
	if (undefined)
		return 0;
	return 1;
}

int get_implied_value(struct expression *expr, long long *val)
{
	int undefined = 0;

	*val =  _get_value(expr, NULL, &undefined, IMPLIED);
	return !undefined;
}

int is_zero(struct expression *expr)
{
	long long val;

	if (get_value(expr, &val) && val == 0)
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
	return expr->unop->left;
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

struct statement *get_block_thing(struct expression *expr)
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

struct expression *strip_expr(struct expression *expr)
{
	if (!expr)
		return NULL;

	switch(expr->type) {
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
	long long val;

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
	if (!get_value(expr, &val))
		return 0;
	if (val < 0)
		return 1;
	if (cur_func->type == SYM_PTR && val == 0)
		return 1;
	return 0;
}
