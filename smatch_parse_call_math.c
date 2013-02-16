/*
 * smatch/smatch_parse_call_math.c
 *
 * Copyright (C) 2012 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

struct {
	const char *func;
	int param;
} alloc_functions[] = {
	{"kmalloc", 0},
	{"__kmalloc", 0},
	{"vmalloc", 0},
	{"__vmalloc", 0},
	{"__vmalloc_node", 0},
};

DECLARE_PTR_LIST(sval_list, sval_t);

static struct sval_list *num_list;
static struct string_list *op_list;

static void push_val(sval_t sval)
{
	sval_t *p;

	p = malloc(sizeof(*p));
	*p = sval;
	add_ptr_list(&num_list, p);
}

static sval_t pop_val()
{
	sval_t *p;
	sval_t ret;

	if (!num_list) {
		sm_msg("internal bug:  %s popping empty list", __func__);
		ret.type = &llong_ctype;
		ret.value = 0;
		return ret;
	}
	p = last_ptr_list((struct ptr_list *)num_list);
	delete_ptr_list_last((struct ptr_list **)&num_list);
	ret = *p;
	free(p);

	return ret;
}

static void push_op(char c)
{
	char *p;

	p = malloc(1);
	p[0] = c;
	add_ptr_list(&op_list, p);
}

static char pop_op()
{
	char *p;
	char c;

	if (!op_list) {
		sm_msg("internal smatch error %s", __func__);
		return '\0';
	}

	p = last_ptr_list((struct ptr_list *)op_list);

	delete_ptr_list_last((struct ptr_list **)&op_list);
	c = p[0];
	free(p);

	return c;
}

static int op_precedence(char c)
{
	switch (c) {
	case '+':
	case '-':
		return 1;
	case '*':
	case '/':
		return 2;
	default:
		return 0;
	}
}

static int top_op_precedence()
{
	char *p;

	if (!op_list)
		return 0;

	p = last_ptr_list((struct ptr_list *)op_list);
	return op_precedence(p[0]);
}

static void pop_until(char c)
{
	char op;
	sval_t left, right;
	sval_t res;

	while (top_op_precedence() && op_precedence(c) <= top_op_precedence()) {
		op = pop_op();
		right = pop_val();
		left = pop_val();
		res = sval_binop(left, op, right);
		push_val(res);
	}
}

static void discard_stacks()
{
	while (op_list)
		pop_op();
	while (num_list)
		pop_val();
}

static int get_implied_param(struct expression *call, int param, sval_t *sval)
{
	struct expression *arg;

	arg = get_argument_from_call_expr(call->args, param);
	return get_implied_value(arg, sval);
}

static int read_number(struct expression *call, char *p, char **end, sval_t *sval)
{
	long param;

	while (*p == ' ')
		p++;

	if (*p == '<') {
		p++;
		param = strtol(p, &p, 10);
		if (!get_implied_param(call, param, sval))
			return 0;
		*end = p + 1;
	} else {
		sval->type = &llong_ctype;
		sval->value = strtoll(p, end, 10);
		if (*end == p)
			return 0;
	}
	return 1;
}

static char *read_op(char *p)
{
	while (*p == ' ')
		p++;

	switch (*p) {
	case '+':
	case '-':
	case '*':
	case '/':
		return p;
	default:
		return NULL;
	}
}

int parse_call_math(struct expression *call, char *math, sval_t *sval)
{
	sval_t tmp;
	char *c;

	/* try to implement shunting yard algorithm. */

	c = (char *)math;
	while (1) {
		if (option_debug)
			sm_msg("parsing %s", c);

		/* read a number and push it onto the number stack */
		if (!read_number(call, c, &c, &tmp))
			goto fail;
		push_val(tmp);

		if (option_debug)
			sm_msg("val = %s remaining = %s", sval_to_str(tmp), c);

		if (!*c)
			break;

		c = read_op(c);
		if (!c)
			goto fail;

		if (option_debug)
			sm_msg("op = %c remaining = %s", *c, c);

		pop_until(*c);
		push_op(*c);
		c++;
	}

	pop_until(0);
	*sval = pop_val();
	return 1;
fail:
	discard_stacks();
	return 0;
}

static struct smatch_state *alloc_state_sname(char *sname)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = sname;
	state->data = INT_PTR(1);
	return state;
}

static int get_arg_number(struct expression *expr)
{
	struct symbol *sym;
	struct symbol *arg;
	int i;

	expr = strip_expr(expr);
	if (expr->type != EXPR_SYMBOL)
		return -1;
	sym = expr->symbol;

	i = 0;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		if (arg == sym)
			return i;
		i++;
	} END_FOR_EACH_PTR(arg);

	return -1;
}

static int format_expr_helper(char *buf, int remaining, struct expression *expr)
{
	int arg;
	sval_t sval;
	int ret;
	char *cur;

	if (!expr)
		return 0;

	cur = buf;

	if (expr->type == EXPR_BINOP) {
		ret = format_expr_helper(cur, remaining, expr->left);
		if (ret == 0)
			return 0;
		remaining -= ret;
		if (remaining <= 0)
			return 0;
		cur += ret;

		ret = snprintf(cur, remaining, " %s ", show_special(expr->op));
		remaining -= ret;
		if (remaining <= 0)
			return 0;
		cur += ret;

		ret = format_expr_helper(cur, remaining, expr->right);
		if (ret == 0)
			return 0;
		remaining -= ret;
		if (remaining <= 0)
			return 0;
		cur += ret;
		return cur - buf;
	}

	arg = get_arg_number(expr);
	if (arg >= 0) {
		ret = snprintf(cur, remaining, "<%d>", arg);
		remaining -= ret;
		if (remaining <= 0)
			return 0;
		return ret;
	}

	if (get_implied_value(expr, &sval)) {
		ret = snprintf(cur, remaining, "%s", sval_to_str(sval));
		remaining -= ret;
		if (remaining <= 0)
			return 0;
		return ret;
	}

	return 0;
}

static char *format_expr(struct expression *expr)
{
	char buf[256];
	int ret;

	ret = format_expr_helper(buf, sizeof(buf), expr);
	if (ret == 0)
		return NULL;

	return alloc_sname(buf);
}

static void match_alloc(const char *fn, struct expression *expr, void *_size_arg)
{
	int size_arg = PTR_INT(_size_arg);
	struct expression *right;
	struct expression *size_expr;
	char *sname;

	right = strip_expr(expr->right);
	size_expr = get_argument_from_call_expr(right->args, size_arg);

	sname = format_expr(size_expr);
	if (!sname)
		return;
	set_state_expr(my_id, expr->left, alloc_state_sname(sname));
}

static char *swap_format(struct expression *call, char *format)
{
	static char buf[256];
	sval_t sval;
	long param;
	struct expression *arg;
	char *p;
	char *out;
	int ret;

	if (format[0] == '<' && format[2] == '>' && format[3] == '\0') {
		param = strtol(format + 1, NULL, 10);
		arg = get_argument_from_call_expr(call->args, param);
		if (!arg)
			return NULL;
		return format_expr(arg);
	}

	buf[0] = '\0';
	p = format;
	out = buf;
	while (*p) {
		if (*p == '<') {
			p++;
			param = strtol(p, &p, 10);
			if (*p != '>')
				return NULL;
			p++;
			arg = get_argument_from_call_expr(call->args, param);
			if (!arg)
				return NULL;
			param = get_arg_number(arg);
			if (param >= 0) {
				ret = snprintf(out, buf + sizeof(buf) - out, "<%ld>", param);
				out += ret;
				if (out >= buf + sizeof(buf))
					return NULL;
			} else if (get_implied_value(arg, &sval)) {
				ret = snprintf(out, buf + sizeof(buf) - out, "%s", sval_to_str(sval));
				out += ret;
				if (out >= buf + sizeof(buf))
					return NULL;
			} else {
				return NULL;
			}
		}
		*out = *p;
		p++;
		out++;
	}
	if (buf[0] == '\0')
		return NULL;
	return alloc_sname(buf);
}

static char *buf_size_recipe;
static int db_buf_size_callback(void *unused, int argc, char **argv, char **azColName)
{
	if (argc != 1)
		return 0;

	if (!buf_size_recipe)
		buf_size_recipe = alloc_sname(argv[0]);
	else if (strcmp(buf_size_recipe, argv[0]) != 0)
		buf_size_recipe = alloc_sname("invalid");
	return 0;
}

static char *get_allocation_recipe_from_call(struct expression *expr)
{
	struct symbol *sym;
	static char sql_filter[1024];
	int i;

	expr = strip_expr(expr);
	if (expr->fn->type != EXPR_SYMBOL)
		return NULL;
	sym = expr->fn->symbol;
	if (!sym)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(alloc_functions); i++) {
		if (strcmp(sym->ident->name, alloc_functions[i].func) == 0) {
			char buf[32];

			snprintf(buf, sizeof(buf), "<%d>", alloc_functions[i].param);
			buf_size_recipe = alloc_sname(buf);
			return swap_format(expr, buf_size_recipe);
		}
	}

	if (sym->ctype.modifiers & MOD_STATIC) {
		snprintf(sql_filter, 1024, "file = '%s' and function = '%s';",
			 get_filename(), sym->ident->name);
	} else {
		snprintf(sql_filter, 1024, "function = '%s' and static = 0;",
				sym->ident->name);
	}

	buf_size_recipe = NULL;
	run_sql(db_buf_size_callback, "select value from return_states where type=%d and %s",
		 BUF_SIZE, sql_filter);
	if (!buf_size_recipe || strcmp(buf_size_recipe, "invalid") == 0)
		return NULL;
	return swap_format(expr, buf_size_recipe);
}

static void match_call_assignment(struct expression *expr)
{
	char *sname;

	sname = get_allocation_recipe_from_call(expr->right);
	if (!sname)
		return;
	set_state_expr(my_id, expr->left, alloc_state_sname(sname));
}

static void match_returns_call(int return_id, char *return_ranges, struct expression *call)
{
	char *sname;

	sname = get_allocation_recipe_from_call(call);
	if (option_debug)
		sm_msg("sname = %s", sname);
	if (!sname)
		return;

	sql_insert_return_states(return_id, return_ranges, BUF_SIZE, -1, "",
			sname);
}

static void print_returned_allocations(int return_id, char *return_ranges, struct expression *expr, struct state_list *slist)
{
	struct smatch_state *state;
	struct symbol *sym;
	char *name;

	expr = strip_expr(expr);
	if (!expr)
		return;

	if (expr->type == EXPR_CALL) {
		match_returns_call(return_id, return_ranges, expr);
		return;
	}

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto free;

	state = get_state_slist(slist, my_id, name, sym);
	if (!state || !state->data)
		goto free;

	sql_insert_return_states(return_id, return_ranges, BUF_SIZE, -1, "",
			state->name);
free:
	free_string(name);
}

void register_parse_call_math(int id)
{
	int i;
	if (!option_info)
		return;

	my_id = id;
	for (i = 0; i < ARRAY_SIZE(alloc_functions); i++)
		add_function_assign_hook(alloc_functions[i].func, &match_alloc,
				         INT_PTR(alloc_functions[i].param));
	add_hook(&match_call_assignment, CALL_ASSIGNMENT_HOOK);
	add_returned_state_callback(print_returned_allocations);
}

