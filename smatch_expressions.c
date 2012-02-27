#include "smatch.h"

static struct position pos;

struct expression *zero_expr()
{
	static struct expression *zero;

	if (zero)
		return zero;

	zero = alloc_expression(pos, EXPR_VALUE);
	zero->value = 0;
	return zero;
}

struct expression *value_expr(long long val)
{
	struct expression *expr;

	if (!val)
		return zero_expr();

	expr = alloc_expression(pos, EXPR_VALUE);
	expr->value = val;
	return expr;
}
