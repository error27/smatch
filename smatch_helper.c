/*
 * sparse/smatch_helper.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include "allocate.h"
#include "smatch.h"

static int star_count;
#define VAR_LEN 512


char *alloc_string(char *str)
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

static void prepend(char *dest, const char *data, int buff_len)
{
	int space_needed;
	int i;
	
	space_needed = strlen(data);
	for (i = buff_len - space_needed - 1; i >= 0 ; i--)
		dest[i + space_needed] = dest[i];
	for (i = 0; i < space_needed % buff_len ; i++)
		dest[i] = data[i];
	dest[buff_len - 1] = '\0';
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
	switch(expr->type) {
	case EXPR_DEREF:
		prepend(buf, expr->member->name, len);
		if (!strcmp(show_special(expr->deref->op), "*"))
			prepend(buf, "->", len);
		else
			prepend(buf, ".", len);		

		//printf("debug:  %d\n", expr->deref

		__get_variable_from_expr(sym_ptr, buf, expr->deref, 
						 len, complicated);
		return;
	case EXPR_SYMBOL:
		if (expr->symbol_name)
			prepend(buf, expr->symbol_name->name, len);
		if (sym_ptr) {
			if (*sym_ptr)
				(*complicated)++;
			*sym_ptr = expr->symbol;
		}
		return;
	case EXPR_PREOP: {
		const char *tmp;

		if (expr->op == '*')
			star_count++;

		__get_variable_from_expr(sym_ptr, buf, expr->unop, 
						 len, complicated);
		tmp = show_special(expr->op);
		if (tmp[0] == '*') {
			if (star_count-- >= 0) {
				prepend(buf, tmp, len);
			}
		} else {
			prepend(buf, tmp, len);
		}

		if (tmp[0] == '(') {
			strncat(buf, ")", len);
			buf[len - 1] = '\0';
		}

		if ((!strcmp(tmp, "--")) || (!strcmp(tmp, "++")))
			(*complicated)++;

		return;
	}
	case EXPR_POSTOP: {
		const char *tmp;

		tmp = show_special(expr->op);
		prepend(buf, tmp, len);
		__get_variable_from_expr(sym_ptr, buf, expr->unop, 
						 len, complicated);

		if ((!strcmp(tmp, "--")) || (!strcmp(tmp, "++")))
			(*complicated)++;

		return;
	}
	case EXPR_BINOP: {
		const char *tmp;

		prepend(buf, ")", len); 
		__get_variable_from_expr(NULL, buf, expr->right, len,
					 complicated);
		tmp = show_special(expr->op);
		prepend(buf, tmp, len);
		__get_variable_from_expr(sym_ptr, buf, expr->left, 
						 len, complicated);
		prepend(buf, "(", len);
		return;
	}
	case EXPR_VALUE: {
		char tmp[25];

		snprintf(tmp, 25, "%lld", expr->value);
		prepend(buf, tmp, len);
		return;
	}
	case EXPR_STRING:
		prepend(buf, expr->string->data, len);
		return;
	case EXPR_CALL: {
		struct expression *tmp;
		int i = 0;
		
		(*complicated)++;
		prepend(buf, ")", len);
		FOR_EACH_PTR_REVERSE(expr->args, tmp) {
			if (i++)
				prepend(buf, ", ", len);
			__get_variable_from_expr(NULL, buf, tmp, len,
						 complicated);
		} END_FOR_EACH_PTR_REVERSE(tmp);
		prepend(buf, "(", len);
		__get_variable_from_expr(NULL, buf, expr->fn, len,
					 complicated);
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
			prepend(buf, tmp, len);
		}
		return;
	}
	default:
		(*complicated)++;
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

char *get_variable_from_expr(struct expression *expr, struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int junk;

	if (sym_ptr)
		*sym_ptr = NULL;
	star_count = 0;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &junk);

	return var_name;
}

/*
 * get_variable_from_expr_simple() only returns simple variables.
 * If it's a complicated variable like a->foo instead of just 'a'
 * then it returns NULL.
 */

char *get_variable_from_expr_simple(struct expression *expr, 
				    struct symbol **sym_ptr)
{
	static char var_name[VAR_LEN];
	int complicated = 0;

	if (sym_ptr)
		*sym_ptr = NULL;
	star_count = 0;
	var_name[0] = '\0';

	if (!expr)
		return NULL;
	__get_variable_from_expr(sym_ptr, var_name, expr, sizeof(var_name),
				 &complicated);
	
	if (complicated) {
		if (sym_ptr)
			*sym_ptr = NULL;
		return NULL;
	}
	return var_name;
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

int get_value(struct expression *expr, int *discard)
{
	int dis = 0;
	int ret = 0;

	if (!expr)
		return 0;
	if (!discard)
		discard = &dis;
	if (*discard)
		return 0;

 	switch (expr->type){
	case EXPR_VALUE:
		ret = expr->value;
		break;
	case EXPR_BINOP:
		if (show_special(expr->op) && 
		    !strcmp("*", show_special(expr->op)))
			ret = get_value(expr->left, discard) 
				* get_value(expr->right, discard);
		break;
	case EXPR_SIZEOF:
		if (expr->cast_type && get_base_type(expr->cast_type))
			ret = (get_base_type(expr->cast_type))->bit_size / 8;
		if (expr->cast_expression)
			;//printf("debugs %d\n", expr->cast_expression->type);
		break;
	default:
		//printf("ouchies-->%d\n", expr->type);
		*discard = 1;
	}
	if (*discard)
		return 0;
	return ret;
}

int is_zero(struct expression *expr)
{
	if (expr->type == EXPR_VALUE && expr->value == 0)
		return 1;
	if (expr->op == '(')
		return is_zero(expr->unop);
	if (expr->type == EXPR_CAST) 
		return is_zero(expr->cast_expression);
	return 0;	
}



