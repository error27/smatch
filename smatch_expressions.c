#include "smatch.h"
#include "smatch_extra.h"

DECLARE_ALLOCATOR(sname);
__ALLOCATOR(struct expression, "temporary expr", tmp_expression);

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

struct expression *alloc_tmp_expression(struct position pos, int type)
{
	struct expression *expr;

	expr = __alloc_tmp_expression(0);
	expr->smatch_flags |= Tmp;
	expr->type = type;
	expr->pos = pos;
	return expr;
}

void free_tmp_expressions(void)
{
	clear_tmp_expression_alloc();
}

struct expression *zero_expr(void)
{
	struct expression *zero;

	zero = alloc_tmp_expression(get_cur_pos(), EXPR_VALUE);
	zero->value = 0;
	zero->ctype = &int_ctype;
	return zero;
}

struct expression *sval_to_expr(sval_t sval)
{
	struct expression *expr;

	expr = alloc_tmp_expression(get_cur_pos(), EXPR_VALUE);
	expr->value = sval.value;
	expr->ctype = sval.type;
	return expr;
}

struct expression *value_expr(long long val)
{
	struct expression *expr;

	if (!val)
		return zero_expr();

	expr = alloc_tmp_expression(get_cur_pos(), EXPR_VALUE);
	expr->value = val;
	expr->ctype = &llong_ctype;
	return expr;
}

static struct expression *symbol_expression_helper(struct symbol *sym, bool perm)
{
	struct expression *expr;

	if (perm)
		expr = alloc_expression(sym->pos, EXPR_SYMBOL);
	else
		expr = alloc_tmp_expression(sym->pos, EXPR_SYMBOL);
	expr->symbol = sym;
	expr->symbol_name = sym->ident;
	return expr;
}

struct expression *symbol_expression(struct symbol *sym)
{
	return symbol_expression_helper(sym, false);
}

struct expression *cast_expression(struct expression *expr, struct symbol *type)
{
	struct expression *cast;

	cast = alloc_tmp_expression(expr->pos, EXPR_CAST);
	cast->cast_type = type;
	cast->cast_expression = expr;
	return cast;
}

struct expression *member_expression(struct expression *deref, int op, struct ident *member)
{
	struct expression *expr;

	expr = alloc_tmp_expression(deref->pos, EXPR_DEREF);
	expr->op = op;
	expr->deref = deref;
	expr->member = member;
	expr->member_offset = -1;
	return expr;
}

struct expression *preop_expression(struct expression *expr, int op)
{
	struct expression *preop;

	preop = alloc_tmp_expression(expr->pos, EXPR_PREOP);
	preop->unop = expr;
	preop->op = op;
	return preop;
}

struct expression *deref_expression(struct expression *expr)
{
	/* The *&foo is just foo */
	if (expr->type == EXPR_PREOP && expr->op == '&')
		return strip_expr(expr->unop);
	if (expr->type == EXPR_BINOP)
		expr = preop_expression(expr, '(');
	return preop_expression(expr, '*');
}

struct expression *deref_expression_no_parens(struct expression *expr)
{
	return preop_expression(expr, '*');
}

struct expression *assign_expression(struct expression *left, int op, struct expression *right)
{
	struct expression *expr;

	if (!right)
		return NULL;

	/* FIXME: make this a tmp expression. */
	expr = alloc_expression(right->pos, EXPR_ASSIGNMENT);
	expr->op = op;
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *assign_expression_perm(struct expression *left, int op, struct expression *right)
{
	struct expression *expr;

	if (!right)
		return NULL;

	expr = alloc_expression(right->pos, EXPR_ASSIGNMENT);
	expr->op = op;
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *create_fake_assign(const char *name, struct symbol *type, struct expression *right)
{
	struct expression *left, *assign;

	if (!right)
		return NULL;

	if (!type) {
		type = get_type(right);
		if (!type)
			return NULL;
	}

	left = fake_variable_perm(type, name);

	assign = assign_expression_perm(left, '=', right);

	assign->smatch_flags |= Fake;

	assign->parent = right->parent;
	expr_set_parent_expr(right, assign);

	__fake_state_cnt++;

	return assign;
}

struct expression *binop_expression(struct expression *left, int op, struct expression *right)
{
	struct expression *expr;

	expr = alloc_tmp_expression(right->pos, EXPR_BINOP);
	expr->op = op;
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *array_element_expression(struct expression *array, struct expression *offset)
{
	struct expression *expr;

	expr = binop_expression(array, '+', offset);
	return deref_expression_no_parens(expr);
}

struct expression *compare_expression(struct expression *left, int op, struct expression *right)
{
	struct expression *expr;

	if (!left || !right)
		return NULL;

	expr = alloc_tmp_expression(get_cur_pos(), EXPR_COMPARE);
	expr->op = op;
	expr->left = left;
	expr->right = right;
	return expr;
}

struct expression *alloc_expression_stmt_perm(struct statement *last_stmt)
{
	struct expression *expr;

	if (!last_stmt)
		return NULL;

	expr = alloc_tmp_expression(last_stmt->pos, EXPR_STATEMENT);
	expr->statement = last_stmt;

	return expr;
}

struct expression *gen_string_expression(char *str)
{
	struct expression *ret;
	struct string *string;
	int len;

	len = strlen(str) + 1;
	string = (void *)__alloc_sname(4 + len);
	string->length = len;
	string->immutable = 0;
	memcpy(string->data, str, len);

	ret = alloc_tmp_expression(get_cur_pos(), EXPR_STRING);
	ret->wide = 0;
	ret->string = string;

	return ret;
}

struct expression *call_expression(struct expression *fn, struct expression_list *args)
{
	struct expression *expr;

	expr = alloc_tmp_expression(fn->pos, EXPR_CALL);
	expr->fn = fn;
	expr->args = args;

	return expr;
}

static struct expression *get_expression_from_base_and_str(struct expression *base, const char *addition)
{
	struct expression *ret = NULL;
	struct token *token, *prev, *end;
	char *alloc;

	if (addition[0] == '\0')
		return base;

	alloc = alloc_string_newline(addition);

	token = tokenize_buffer(alloc, strlen(alloc), &end);
	if (!token)
		goto free;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		goto free;
	token = token->next;

	ret = base;
	while (token_type(token) == TOKEN_SPECIAL &&
	       (token->special == SPECIAL_DEREFERENCE || token->special == '.')) {
		prev = token;
		token = token->next;
		if (token_type(token) != TOKEN_IDENT)
			goto free;
		switch (prev->special) {
		case SPECIAL_DEREFERENCE:
			ret = deref_expression(ret);
			ret = member_expression(ret, '*', token->ident);
			break;
		case '.':
			ret = member_expression(ret, '.', token->ident);
			break;
		default:
			goto free;
		}
		token = token->next;
	}

	if (token_type(token) != TOKEN_STREAMEND)
		goto free;

free:
	free_string(alloc);

	return ret;
}

static struct expression *gen_expression_from_name_sym_helper(const char *name, struct symbol *sym)
{
	struct expression *ret;
	int skip = 0;

	if (!sym)
		return NULL;

	if (name[0] == '&' ||
	    name[0] == '*' ||
	    name[0] == '(') {
		ret = gen_expression_from_name_sym_helper(name + 1, sym);
		return preop_expression(ret, name[0]);
	}
	while (name[skip] != '\0' && name[skip] != '.' && name[skip] != '-')
		skip++;

	return get_expression_from_base_and_str(symbol_expression(sym), name + skip);
}

struct expression *gen_expression_from_name_sym(const char *name, struct symbol *sym)
{
	struct expression *ret;

	ret = gen_expression_from_name_sym_helper(name, sym);
	if (ret) {
		char *new = expr_to_str(ret);

		/*
		 * FIXME: this sometimes changes "foo->bar.a.b->c" into
		 * "foo->bar.a.b.c".  I don't know why...  :(
		 *
		 */
		if (!new || strcmp(name, new) != 0)
			return NULL;
	}
	return ret;
}

struct expression *gen_expression_from_key(struct expression *arg, const char *key)
{
	struct expression *ret;
	struct token *token, *end;
	const char *p = key;
	char buf[4095];
	char *alloc;
	int cnt = 0;
	size_t len;
	bool star;

	if (strcmp(key, "$") == 0)
		return arg;

	if (strcmp(key, "*$") == 0) {
		if (arg->type == EXPR_PREOP &&
		    arg->op == '&')
			return strip_expr(arg->unop);
		return deref_expression(arg);
	}

	/* The idea is that we can parse either $0->foo or $->foo */
	if (key[0] != '$')
		return NULL;
	p++;
	while (*p >= '0' && *p <= '9')
		p++;
	len = snprintf(buf, sizeof(buf), "%s\n", p);
	alloc = alloc_string(buf);

	token = tokenize_buffer(alloc, len, &end);
	if (!token)
		return NULL;
	if (token_type(token) != TOKEN_STREAMBEGIN)
		return NULL;
	token = token->next;

	ret = arg;
	while (token_type(token) == TOKEN_SPECIAL &&
	       (token->special == SPECIAL_DEREFERENCE || token->special == '.')) {
		if (token->special == SPECIAL_DEREFERENCE)
			star = true;
		else
			star = false;

		if (cnt++ == 0 && ret->type == EXPR_PREOP && ret->op == '&') {
			ret = strip_expr(ret->unop);
			star = false;
		}

		token = token->next;
		if (token_type(token) != TOKEN_IDENT)
			return NULL;

		if (star)
			ret = deref_expression(ret);
		ret = member_expression(ret, star ? '*' : '.', token->ident);
		token = token->next;
	}

	if (token_type(token) != TOKEN_STREAMEND)
		return NULL;

	return ret;
}

struct expression *gen_expr_from_param_key(struct expression *expr, int param, const char *key)
{
	struct expression *call, *arg;

	if (!expr)
		return NULL;

	call = expr;
	while (call->type == EXPR_ASSIGNMENT)
		call = strip_expr(call->right);
	if (call->type != EXPR_CALL)
		return NULL;

	if (param == -1) {
		if (expr->type != EXPR_ASSIGNMENT)
			return NULL;
		arg = expr->left;
	} else {
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return NULL;
	}

	return gen_expression_from_key(arg, key);
}

bool is_fake_var(struct expression *expr)
{
	if (expr && (expr->smatch_flags & Fake))
		return true;
	return false;
}

bool is_fake_var_assign(struct expression *expr)
{
	struct expression *left;
	struct symbol *sym;

	if (!expr || expr->type != EXPR_ASSIGNMENT || expr->op != '=')
		return false;
	left = expr->left;
	if (left->type != EXPR_SYMBOL)
		return false;
	if (!is_fake_var(left))
		return false;

	sym = left->symbol;
	if (strncmp(sym->ident->name, "__fake_", 7) != 0)
		return false;
	return true;
}

static struct expression *fake_variable_helper(struct symbol *type, const char *name, bool perm)
{
	struct symbol *sym, *node;
	struct expression *ret;
	struct ident *ident;

	if (!type)
		type = &llong_ctype;

	ident = alloc_ident(name, strlen(name));

	sym = alloc_symbol(get_cur_pos(), type->type);
	sym->ident = ident;
	sym->ctype.base_type = type;
	sym->ctype.modifiers |= MOD_AUTO;

	node = alloc_symbol(get_cur_pos(), SYM_NODE);
	node->ident = ident;
	node->ctype.base_type = type;
	node->ctype.modifiers |= MOD_AUTO;

	if (perm)
		ret = symbol_expression_helper(node, true);
	else
		ret = symbol_expression(node);

	ret->smatch_flags |= Fake;

	return ret;
}

struct expression *fake_variable(struct symbol *type, const char *name)
{
	return fake_variable_helper(type, name, false);
}

struct expression *fake_variable_perm(struct symbol *type, const char *name)
{
	return fake_variable_helper(type, name, true);
}

void expr_set_parent_expr(struct expression *expr, struct expression *parent)
{
	struct expression *prev;

	if (!expr || !parent)
		return;

	prev = expr_get_parent_expr(expr);
	if (prev == parent)
		return;

	if (parent && parent->smatch_flags & Tmp)
		return;

	expr->parent = (unsigned long)parent | 0x1UL;
}

void expr_set_parent_stmt(struct expression *expr, struct statement *parent)
{
	if (!expr)
		return;
	expr->parent = (unsigned long)parent;
}

struct expression *expr_get_parent_expr(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (!(expr->parent & 0x1UL))
		return NULL;

	parent = (struct expression *)(expr->parent & ~0x1UL);
	if (parent && (parent->smatch_flags & Fake))
		return expr_get_parent_expr(parent);
	return parent;
}

struct expression *expr_get_fake_parent_expr(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (!(expr->parent & 0x1UL))
		return NULL;

	parent = (struct expression *)(expr->parent & ~0x1UL);
	if (parent && (parent->smatch_flags & Fake))
		return parent;
	return NULL;
}

struct statement *expr_get_parent_stmt(struct expression *expr)
{
	struct expression *parent;

	if (!expr)
		return NULL;
	if (expr->parent & 0x1UL) {
		parent = (struct expression *)(expr->parent & ~0x1UL);
		if (parent->smatch_flags & Fake)
			return expr_get_parent_stmt(parent);
		return NULL;
	}
	return (struct statement *)expr->parent;
}

struct statement *get_parent_stmt(struct expression *expr)
{
	struct expression *tmp;
	int count = 10;

	if (!expr)
		return NULL;
	while (--count > 0 && (tmp = expr_get_parent_expr(expr)))
		expr = tmp;
	if (!count)
		return NULL;

	return expr_get_parent_stmt(expr);
}
