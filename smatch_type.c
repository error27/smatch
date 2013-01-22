/*
 * sparse/smatch_types.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * The idea here is that you have an expression and you
 * want to know what the type is for that.
 */

#include "smatch.h"

struct symbol *get_real_base_type(struct symbol *sym)
{
	struct symbol *ret;

	ret = get_base_type(sym);
	if (ret && ret->type == SYM_RESTRICT)
		return get_real_base_type(ret);
	return ret;
}

int type_bits(struct symbol *type)
{
	if (!type)
		return 0;
	if (type->type == SYM_PTR)  /* Sparse doesn't set this for &pointers */
		return bits_in_pointer;
	return type->bit_size;
}

int type_positive_bits(struct symbol *type)
{
	if (!type)
		return 0;
	if (type_unsigned(type))
		return type_bits(type);
	return type_bits(type) - 1;
}

static struct symbol *get_binop_type(struct expression *expr)
{
	struct symbol *left, *right;

	left = get_type(expr->left);
	right = get_type(expr->right);

	if (!left || !right)
		return NULL;

	if (left->type == SYM_PTR || left->type == SYM_ARRAY)
		return left;
	if (right->type == SYM_PTR || right->type == SYM_ARRAY)
		return right;

	if (expr->op == SPECIAL_LEFTSHIFT ||
	    expr->op == SPECIAL_RIGHTSHIFT) {
		if (type_positive_bits(left) < 31)
			return &int_ctype;
		return left;
	}

	if (type_positive_bits(left) < 31 && type_positive_bits(right) < 31)
		return &int_ctype;

	if (type_positive_bits(left) > type_positive_bits(right))
		return left;
	return right;
}

static struct symbol *get_type_symbol(struct expression *expr)
{
	if (!expr || expr->type != EXPR_SYMBOL || !expr->symbol)
		return NULL;

	return get_real_base_type(expr->symbol);
}

static struct symbol *get_member_symbol(struct symbol_list *symbol_list, struct ident *member)
{
	struct symbol *tmp, *sub;

	FOR_EACH_PTR(symbol_list, tmp) {
		if (!tmp->ident) {
			sub = get_real_base_type(tmp);
			sub = get_member_symbol(sub->symbol_list, member);
			if (sub)
				return sub;
			continue;
		}
		if (tmp->ident == member)
			return tmp;
	} END_FOR_EACH_PTR(tmp);

	return NULL;
}

static struct symbol *get_symbol_from_deref(struct expression *expr)
{
	struct ident *member;
	struct symbol *sym;

	if (!expr || expr->type != EXPR_DEREF)
		return NULL;

	member = expr->member;
	sym = get_type(expr->deref);
	if (!sym) {
		// sm_msg("could not find struct type");
		return NULL;
	}
	if (sym->type == SYM_PTR)
		sym = get_real_base_type(sym);
	sym = get_member_symbol(sym->symbol_list, member);
	if (!sym)
		return NULL;
	return get_real_base_type(sym);
}

static struct symbol *get_return_type(struct expression *expr)
{
	struct symbol *tmp;

	tmp = get_type(expr->fn);
	if (!tmp)
		return NULL;
	return get_real_base_type(tmp);
}

static struct symbol *get_expr_stmt_type(struct statement *stmt)
{
	if (stmt->type != STMT_COMPOUND)
		return NULL;
	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (!stmt || stmt->type != STMT_EXPRESSION)
		return NULL;
	return get_type(stmt->expression);
}

static struct symbol *get_select_type(struct expression *expr)
{
	struct symbol *one, *two;

	one = get_type(expr->cond_true);
	two = get_type(expr->cond_false);
	if (!one || !two)
		return NULL;
	/*
	 * This is a hack.  If the types are not equiv then we
	 * really don't know the type.  But I think guessing is
	 *  probably Ok here.
	 */
	if (type_positive_bits(one) > type_positive_bits(two))
		return one;
	return two;
}

struct symbol *get_pointer_type(struct expression *expr)
{
	struct symbol *sym;

	sym = get_type(expr);
	if (!sym || (sym->type != SYM_PTR && sym->type != SYM_ARRAY))
		return NULL;
	return get_real_base_type(sym);
}

static struct symbol *fake_pointer_sym(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *base;

	sym = alloc_symbol(expr->pos, SYM_PTR);
	expr = expr->unop;
	base = get_type(expr);
	if (!base)
		return NULL;
	sym->ctype.base_type = base;
	return sym;
}

struct symbol *get_type(struct expression *expr)
{
	if (!expr)
		return NULL;
	expr = strip_parens(expr);

	switch (expr->type) {
	case EXPR_SYMBOL:
		return get_type_symbol(expr);
	case EXPR_DEREF:
		return get_symbol_from_deref(expr);
	case EXPR_PREOP:
	case EXPR_POSTOP:
		if (expr->op == '&')
			return fake_pointer_sym(expr);
		if (expr->op == '*')
			return get_pointer_type(expr->unop);
		return get_type(expr->unop);
	case EXPR_ASSIGNMENT:
		return get_type(expr->left);
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
	case EXPR_IMPLIED_CAST:
		return get_real_base_type(expr->cast_type);
	case EXPR_COMPARE:
	case EXPR_BINOP:
		return get_binop_type(expr);
	case EXPR_CALL:
		return get_return_type(expr);
	case EXPR_STATEMENT:
		return get_expr_stmt_type(expr->statement);
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		return get_select_type(expr);
	case EXPR_SIZEOF:
		return &ulong_ctype;
	case EXPR_LOGICAL:
		return &int_ctype;
	default:
//		sm_msg("unhandled type %d", expr->type);
		return expr->ctype;
	}
	return NULL;
}

int type_unsigned(struct symbol *base_type)
{
	if (!base_type)
		return 0;
	if (base_type->ctype.modifiers & MOD_UNSIGNED)
		return 1;
	return 0;
}

int type_signed(struct symbol *base_type)
{
	if (!base_type)
		return 0;
	if (base_type->ctype.modifiers & MOD_UNSIGNED)
		return 0;
	return 1;
}

int expr_unsigned(struct expression *expr)
{
	struct symbol *sym;

	sym = get_type(expr);
	if (!sym)
		return 0;
	if (type_unsigned(sym))
		return 1;
	return 0;
}

int returns_unsigned(struct symbol *sym)
{
	if (!sym)
		return 0;
	sym = get_base_type(sym);
	if (!sym || sym->type != SYM_FN)
		return 0;
	sym = get_base_type(sym);
	return type_unsigned(sym);
}

int is_pointer(struct expression *expr)
{
	struct symbol *sym;

	sym = get_type(expr);
	if (!sym)
		return 0;
	if (sym->type == SYM_PTR)
		return 1;
	return 0;
}

int returns_pointer(struct symbol *sym)
{
	if (!sym)
		return 0;
	sym = get_base_type(sym);
	if (!sym || sym->type != SYM_FN)
		return 0;
	sym = get_base_type(sym);
	if (sym->type == SYM_PTR)
		return 1;
	return 0;
}

sval_t sval_type_max(struct symbol *base_type)
{
	sval_t ret;

	ret.value = (~0ULL) >> 1;
	ret.type = base_type;

	if (!base_type || !base_type->bit_size)
		return ret;

	ret.value = (~0ULL) >> (64 - type_positive_bits(base_type));
	return ret;
}

sval_t sval_type_min(struct symbol *base_type)
{
	sval_t ret;

	if (!base_type || !base_type->bit_size)
		base_type = &llong_ctype;
	ret.type = base_type;

	if (type_unsigned(base_type)) {
		ret.value = 0;
		return ret;
	}

	ret.value = (~0ULL) << type_positive_bits(base_type);

	return ret;
}

int nr_bits(struct expression *expr)
{
	struct symbol *type;

	type = get_type(expr);
	if (!type)
		return 0;
	return type_bits(type);
}

int is_static(struct expression *expr)
{
	char *name;
	struct symbol *sym;
	int ret = 0;

	name = expr_to_str_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	if (sym->ctype.modifiers & MOD_STATIC)
		ret = 1;
free:
	free_string(name);
	return ret;
}

int types_equiv(struct symbol *one, struct symbol *two)
{
	if (!one && !two)
		return 1;
	if (!one || !two)
		return 0;
	if (one->type != two->type)
		return 0;
	if (one->type == SYM_PTR)
		return types_equiv(get_real_base_type(one), get_real_base_type(two));
	if (type_positive_bits(one) != type_positive_bits(two))
		return 0;
	return 1;
}

const char *global_static()
{
	if (cur_func_sym->ctype.modifiers & MOD_STATIC)
		return "static";
	else
		return "global";
}

struct symbol *cur_func_return_type(void)
{
	struct symbol *sym;

	sym = get_real_base_type(cur_func_sym);
	if (!sym || sym->type != SYM_FN)
		return NULL;
	sym = get_real_base_type(sym);
	return sym;
}

struct symbol *get_arg_type(struct expression *fn, int arg)
{
	struct symbol *fn_type;
	struct symbol *tmp;
	struct symbol *arg_type;
	int i;

	fn_type = get_type(fn);
	if (!fn_type)
		return NULL;
	if (fn_type->type == SYM_PTR)
		fn_type = get_real_base_type(fn_type);
	if (fn_type->type != SYM_FN)
		return NULL;

	i = 0;
	FOR_EACH_PTR(fn_type->arguments, tmp) {
		arg_type = get_real_base_type(tmp);
		if (i == arg) {
			return arg_type;
		}
		i++;
	} END_FOR_EACH_PTR(tmp);

	return NULL;
}

static struct symbol *get_member_from_string(struct symbol_list *symbol_list, char *name)
{
	struct symbol *tmp, *sub;
	int chunk_len;

	if (strncmp(name, ".", 1) == 0)
		name += 1;
	if (strncmp(name, "->", 2) == 0)
		name += 2;

	FOR_EACH_PTR(symbol_list, tmp) {
		if (!tmp->ident) {
			sub = get_real_base_type(tmp);
			sub = get_member_from_string(sub->symbol_list, name);
			if (sub)
				return sub;
			continue;
		}

		if (strcmp(tmp->ident->name, name) == 0)
			return tmp;

		chunk_len = strlen(tmp->ident->name);
		if (strncmp(tmp->ident->name, name, chunk_len) == 0 &&
		    (name[chunk_len] == '.' || name[chunk_len] == '-')) {
			sub = get_real_base_type(tmp);
			return get_member_from_string(sub->symbol_list, name + chunk_len);
		}

	} END_FOR_EACH_PTR(tmp);

	return NULL;
}

struct symbol *get_member_type_from_key(struct expression *expr, char *key)
{
	struct symbol *sym;
	char *name;

	if (strcmp(key, "$$") == 0)
		return get_type(expr);

	if (strcmp(key, "*$$") == 0) {
		sym = get_type(expr);
		if (!sym || sym->type != SYM_PTR)
			return NULL;
		return get_real_base_type(sym);
	}

	name = expr_to_str_sym(expr, &sym);
	free_string(name);
	if (!sym)
		return NULL;
	sym = get_real_base_type(sym);
	if (sym->type == SYM_PTR)
		sym = get_real_base_type(sym);

	key = key + 2;
	sym = get_member_from_string(sym->symbol_list, key);
	if (!sym)
		return NULL;
	return get_real_base_type(sym);
}
