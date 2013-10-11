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

struct expression *zero_expr()
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

#define FAKE_NAME "smatch_fake"
struct ident unknown_value = {
	.len = sizeof(FAKE_NAME),
	.name = FAKE_NAME,
};

static int fake_counter;
static struct ident *fake_ident()
{
	struct ident *ret;
	char buf[32];
	int len;

	snprintf(buf, sizeof(buf), "smatch_fake_%d", fake_counter++);
	len = strlen(buf) + 1;
	ret = malloc(sizeof(*ret) + len);
	memset(ret, 0, sizeof(*ret));
	memcpy(ret->name, buf, len);
	ret->len = len;

	return ret;
}

struct expression *unknown_value_expression(struct expression *expr)
{
	struct expression *ret;
	struct symbol *type;

	type = get_type(expr);
	if (!type || type->type != SYM_BASETYPE)
		type = &llong_ctype;

	ret = alloc_expression(expr->pos, EXPR_SYMBOL);
	ret->symbol = alloc_symbol(expr->pos, SYM_BASETYPE);
	ret->symbol->ctype.base_type = type;
	ret->symbol_name = fake_ident();

	return ret;
}
