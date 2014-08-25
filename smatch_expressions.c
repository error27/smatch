#include "smatch.h"

static struct position get_cur_pos(void)
{
	static struct position pos;
	static struct position none;
	struct expression *expr;
	struct statement *stmt;

	expr = last_ptr_list((struct ptr_list *)big_expression_stack);
	stmt = last_ptr_list((struct ptr_list *)big_statement_stack);
	if (expr)
		pos = expr->pos;
	else if (stmt)
		pos = stmt->pos;
	else
		pos = none;
	return pos;
}

struct expression *zero_expr(void)
{
	static struct expression *zero;

	if (zero)
		return zero;

	zero = alloc_expression(get_cur_pos(), EXPR_VALUE);
	zero->value = 0;
	zero->ctype = &char_ctype;
	return zero;
}

struct expression *value_expr(long long val)
{
	struct expression *expr;

	if (!val)
		return zero_expr();

	expr = alloc_expression(get_cur_pos(), EXPR_VALUE);
	expr->value = val;
	expr->ctype = &llong_ctype;
	return expr;
}

struct expression *member_expression(struct expression *deref, int op, struct ident *member)
{
	struct expression *expr;

	expr = alloc_expression(deref->pos, EXPR_DEREF);
	expr->op = op;
	expr->deref = deref;
	expr->member = member;
	return expr;
}

struct expression *deref_expression(struct expression *expr)
{
	struct expression *preop;

	preop = alloc_expression(expr->pos, EXPR_PREOP);
	preop->unop = expr;
	preop->op = '*';
	return preop;
}

struct expression *assign_expression(struct expression *left, struct expression *right)
{
	struct expression *expr;

	expr = alloc_expression(right->pos, EXPR_ASSIGNMENT);
	expr->op = '=';
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *binop_expression(struct expression *left, int op, struct expression *right)
{
	struct expression *expr;

	expr = alloc_expression(right->pos, EXPR_BINOP);
	expr->op = op;
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *array_element_expression(struct expression *array, struct expression *offset)
{
	struct expression *expr;

	expr = binop_expression(array, '+', offset);
	return deref_expression(expr);
}

struct expression *symbol_expression(struct symbol *sym)
{
	struct expression *expr;

	expr = alloc_expression(sym->pos, EXPR_SYMBOL);
	expr->symbol = sym;
	expr->symbol_name = sym->ident;
	return expr;
}

