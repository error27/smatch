/*
 * Copyright (C) 2006,2008 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#define _GNU_SOURCE 1
#include <unistd.h>
#include <stdio.h>
#include "token.h"
#include "scope.h"
#include "smatch.h"
#include "smatch_expression_stacks.h"
#include "smatch_extra.h"
#include "smatch_slist.h"

int __in_fake_assign;
int __in_fake_struct_assign;
int __in_buf_clear;
int __in_fake_var_assign;
int __in_array_initializer;
int __fake_state_cnt;
int __debug_skip;
int in_fake_env;
int final_pass;
int __inline_call;
bool __reparsing_code;
struct expression  *__inline_fn;

int __smatch_lineno = 0;
static struct position current_pos;

static char *base_file;
static const char *filename;
static char *pathname;
static char *full_filename;
static char *full_base_file;
static char *cur_func;
int base_file_stream;
static unsigned int loop_count;
static int last_goto_statement_handled;
int __expr_stmt_count;
int __in_function_def;
int __in_unmatched_hook;
static struct expression_list *switch_expr_stack = NULL;
static struct expression_list *post_op_stack = NULL;

static struct ptr_list *fn_data_list;
static struct ptr_list *backup;

struct expression_list *big_expression_stack;
struct statement_list *big_statement_stack;
struct statement *__prev_stmt;
struct statement *__cur_stmt;
struct statement *__next_stmt;
static struct expression_list *parsed_calls;
static int indent_cnt;
int __in_pre_condition = 0;
int __bail_on_rest_of_function = 0;
static struct timeval fn_start_time;
static struct timeval outer_fn_start_time;
char *get_function(void) { return cur_func; }
int get_lineno(void) { return __smatch_lineno; }
int inside_loop(void) { return !!loop_count; }
int definitely_inside_loop(void) { return !!(loop_count & ~0x08000000); }
struct expression *get_switch_expr(void) { return top_expression(switch_expr_stack); }
int in_expression_statement(void) { return !!__expr_stmt_count; }

static void split_symlist(struct symbol_list *sym_list);
static void split_declaration(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list, struct expression *parent);
static void split_args(struct expression *expr);
static struct expression *fake_a_variable_assign(struct symbol *type, struct expression *call, struct expression *expr, int nr);
static void add_inline_function(struct symbol *sym);
static void parse_inline(struct expression *expr);

int option_assume_loops = 0;
int option_two_passes = 0;
struct symbol *cur_func_sym = NULL;
struct stree *global_states;

const unsigned long valid_ptr_min = 4096;
unsigned long valid_ptr_max = ULONG_MAX & ~(MTAG_OFFSET_MASK);
const sval_t valid_ptr_min_sval = {
	.type = &ptr_ctype,
	{.value = 4096},
};
sval_t ptr_err_min = { .type = &ptr_ctype };
sval_t ptr_err_max = { .type = &ptr_ctype };
sval_t ptr_xa_err_min = { .type = &ptr_ctype };
sval_t ptr_xa_err_max = { .type = &ptr_ctype };
sval_t ulong_ULONG_MAX = { .type = &ulong_ctype };

sval_t valid_ptr_max_sval = {
	.type = &ptr_ctype,
	{.value = ULONG_MAX & ~(MTAG_OFFSET_MASK)},
};
struct range_list *valid_ptr_rl;

void alloc_ptr_constants(void)
{
	valid_ptr_max = sval_type_max(&ulong_ctype).value & ~(MTAG_OFFSET_MASK);
	valid_ptr_max_sval.value = valid_ptr_max;

	valid_ptr_rl = alloc_rl(valid_ptr_min_sval, valid_ptr_max_sval);
	valid_ptr_rl = cast_rl(&ptr_ctype, valid_ptr_rl);
	valid_ptr_rl = clone_rl_permanent(valid_ptr_rl);

	ptr_err_min = sval_cast(&ptr_ctype, err_min);
	ptr_err_max = sval_cast(&ptr_ctype, err_max);
	ptr_xa_err_min = sval_cast(&ptr_ctype, xa_err_min);
	ptr_xa_err_max = sval_cast(&ptr_ctype, xa_err_max);
	ulong_ULONG_MAX = sval_type_max(&ulong_ctype);
}

int outside_of_function(void)
{
	return cur_func_sym == NULL;
}

const char *get_filename(void)
{
	if (option_info && option_full_path)
		return full_base_file;
	if (option_info)
		return base_file;
	if (option_full_path)
		return full_filename;
	return filename;
}

const char *get_base_file(void)
{
	if (option_full_path)
		return full_base_file;
	return base_file;
}

unsigned long long get_file_id(void)
{
	return str_to_llu_hash(get_filename());
}

unsigned long long get_base_file_id(void)
{
	return str_to_llu_hash(get_base_file());
}

static void set_position(struct position pos)
{
	int len;
	static int prev_stream = -1;

	if (in_fake_env)
		return;

	if (pos.stream == 0 && pos.line == 0)
		return;

	__smatch_lineno = pos.line;
	current_pos = pos;

	if (pos.stream == prev_stream)
		return;

	prev_stream = pos.stream;
	filename = stream_name(pos.stream);

	free(full_filename);
	pathname = getcwd(NULL, 0);
	if (pathname) {
		len = strlen(pathname) + 1 + strlen(filename) + 1;
		full_filename = malloc(len);
		snprintf(full_filename, len, "%s/%s", pathname, filename);
	} else {
		full_filename = alloc_string(filename);
	}
	free(pathname);
}

int is_assigned_call(struct expression *expr)
{
	struct expression *parent = expr_get_parent_expr(expr);

	if (parent &&
	    parent->type == EXPR_ASSIGNMENT &&
	    parent->op == '=' &&
	    strip_expr(parent->right) == expr)
		return 1;

	return 0;
}

struct expression *get_parent_assignment(struct expression *expr)
{
	struct expression *parent;
	int cnt = 0;

	if (expr->type == EXPR_ASSIGNMENT)
		return NULL;

	parent = expr;
	while (true) {
		parent = expr_get_fake_or_real_parent_expr(parent);
		if (!parent || ++cnt >= 5)
			break;
		if (parent->type == EXPR_CAST)
			continue;
		if (parent->type == EXPR_PREOP && parent->op == '(')
			continue;
		break;
	}

	if (parent && parent->type == EXPR_ASSIGNMENT)
		return parent;
	return NULL;
}

int is_fake_assigned_call(struct expression *expr)
{
	struct expression *parent = expr_get_fake_parent_expr(expr);

	if (parent &&
	    parent->type == EXPR_ASSIGNMENT &&
	    parent->op == '=' &&
	    strip_expr(parent->right) == expr)
		return 1;

	return 0;
}

static bool is_inline_func(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return false;
	if (!expr->symbol->definition)
		return false;
	if (expr->symbol->definition->ctype.modifiers & MOD_INLINE)
		return true;

	return 0;
}

static int is_noreturn_func(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;

	/*
	 * It's almost impossible for Smatch to handle __builtin_constant_p()
	 * the same way that GCC does so Smatch ends up making some functions
	 * as no return functions incorrectly.
	 *
	 */
	if (option_project == PROJ_KERNEL && expr->symbol->ident &&
	    strstr(expr->symbol->ident->name, "__compiletime_assert"))
		return 0;

	if (expr->symbol->ctype.modifiers & MOD_NORETURN)
		return 1;
	if (expr->symbol->ident &&
	    strcmp(expr->symbol->ident->name, "__builtin_unreachable") == 0)
		return 1;

	return 0;
}

static int save_func_time(void *_rl, int argc, char **argv, char **azColName)
{
	unsigned long *rl = _rl;

	*rl = strtoul(argv[0], NULL, 10);
	return 0;
}

static int get_func_time(struct symbol *sym)
{
	unsigned long time = 0;

	run_sql(&save_func_time, &time,
		"select key from return_implies where %s and type = %d;",
		get_static_filter(sym), FUNC_TIME);

	return time;
}

static int inline_budget = 20;

int inlinable(struct expression *expr)
{
	struct symbol *sym;
	struct statement *last_stmt = NULL;

	if (__inline_fn)  /* don't nest */
		return 0;

	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;
	if (is_no_inline_function(expr->symbol->ident->name))
		return 0;
	sym = get_base_type(expr->symbol);
	if (sym->stmt && sym->stmt->type == STMT_COMPOUND) {
		if (ptr_list_size((struct ptr_list *)sym->stmt->stmts) > 10)
			return 0;
		if (sym->stmt->type != STMT_COMPOUND)
			return 0;
		last_stmt = last_ptr_list((struct ptr_list *)sym->stmt->stmts);
	}
	if (sym->inline_stmt && sym->inline_stmt->type == STMT_COMPOUND) {
		if (ptr_list_size((struct ptr_list *)sym->inline_stmt->stmts) > 10)
			return 0;
		if (sym->inline_stmt->type != STMT_COMPOUND)
			return 0;
		last_stmt = last_ptr_list((struct ptr_list *)sym->inline_stmt->stmts);
	}

	if (!last_stmt)
		return 0;

	/* the magic numbers in this function are pulled out of my bum. */
	if (last_stmt->pos.line > sym->pos.line + inline_budget)
		return 0;

	if (get_func_time(expr->symbol) >= 2)
		return 0;

	return 1;
}

void __process_post_op_stack(void)
{
	struct expression *expr;

	FOR_EACH_PTR(post_op_stack, expr) {
		__pass_to_client(expr, OP_HOOK);
	} END_FOR_EACH_PTR(expr);

	__free_ptr_list((struct ptr_list **)&post_op_stack);
}

static int handle_comma_assigns(struct expression *expr)
{
	struct expression *right;
	struct expression *assign;

	right = strip_expr(expr->right);
	if (right->type != EXPR_COMMA)
		return 0;

	__split_expr(right->left);
	__process_post_op_stack();

	assign = assign_expression(expr->left, '=', right->right);
	__split_expr(assign);

	return 1;
}

/* This is to handle *p++ = foo; assignments */
static int handle_postop_assigns(struct expression *expr)
{
	struct expression *left, *fake_left;
	struct expression *assign;

	left = strip_expr(expr->left);
	if (left->type != EXPR_PREOP || left->op != '*')
		return 0;
	left = strip_expr(left->unop);
	if (left->type != EXPR_POSTOP)
		return 0;

	fake_left = deref_expression(strip_expr(left->unop));
	assign = assign_expression(fake_left, '=', expr->right);

	__split_expr(assign);
	__split_expr(expr->left);

	return 1;
}

static bool parent_is_dereference(struct expression *expr)
{
	struct expression *parent;

	parent = expr;
	while ((parent = expr_get_parent_expr(parent))) {
		if (parent->type == EXPR_DEREF)
			return true;
		if (parent->type == EXPR_PREOP &&
		    parent->op == '*')
			return true;
	}

	return false;
}

static int prev_expression_is_getting_address(struct expression *expr)
{
	struct expression *parent;

	do {
		parent = expr_get_parent_expr(expr);

		if (!parent)
			return 0;
		if (parent->type == EXPR_PREOP && parent->op == '&') {
			if (parent_is_dereference(parent))
				return false;
			return true;
		}
		if (parent->type == EXPR_PREOP && parent->op == '(')
			goto next;
		if (parent->type == EXPR_DEREF && parent->op == '.')
			goto next;
		/* Handle &foo->array[offset] */
		if (parent->type == EXPR_BINOP && parent->op == '+') {
			parent = expr_get_parent_expr(parent);
			if (!parent)
				return 0;
			if (parent->type == EXPR_PREOP && parent->op == '*')
				goto next;
		}

		return 0;
next:
		expr = parent;
	} while (1);
}

int __in_builtin_overflow_func;
static void handle_builtin_overflow_func(struct expression *expr)
{
	struct expression *a, *b, *res, *assign;
	int op;

	if (sym_name_is("__builtin_add_overflow", expr->fn))
		op = '+';
	else if (sym_name_is("__builtin_sub_overflow", expr->fn))
		op = '-';
	else if (sym_name_is("__builtin_mul_overflow", expr->fn))
		op = '*';
	else
		return;

	a = get_argument_from_call_expr(expr->args, 0);
	b = get_argument_from_call_expr(expr->args, 1);
	res = get_argument_from_call_expr(expr->args, 2);

	assign = assign_expression(deref_expression(res), '=', binop_expression(a, op, b));

	__in_builtin_overflow_func++;
	__split_expr(assign);
	__in_builtin_overflow_func--;
}

static int handle__builtin_choose_expr(struct expression *expr)
{
	struct expression *const_expr, *expr1, *expr2;
	sval_t sval;

	if (!sym_name_is("__builtin_choose_expr", expr->fn))
		return 0;

	const_expr = get_argument_from_call_expr(expr->args, 0);
	expr1 = get_argument_from_call_expr(expr->args, 1);
	expr2 = get_argument_from_call_expr(expr->args, 2);

	if (!get_value(const_expr, &sval) || !expr1 || !expr2)
		return 0;
	if (sval.value)
		__split_expr(expr1);
	else
		__split_expr(expr2);
	return 1;
}

static int handle__builtin_choose_expr_assigns(struct expression *expr)
{
	struct expression *const_expr, *right, *expr1, *expr2, *fake;
	sval_t sval;

	/*
	 * We can't use strip_no_cast() because it strips out
	 * __builtin_choose_expr() which turns this function into a no-op.
	 *
	 */
	right = strip_parens(expr->right);
	if (right->type != EXPR_CALL)
		return 0;
	if (!sym_name_is("__builtin_choose_expr", right->fn))
		return 0;

	const_expr = get_argument_from_call_expr(right->args, 0);
	expr1 = get_argument_from_call_expr(right->args, 1);
	expr2 = get_argument_from_call_expr(right->args, 2);

	if (!get_value(const_expr, &sval) || !expr1 || !expr2)
		return 0;

	fake = assign_expression(expr->left, '=', sval.value ? expr1 : expr2);
	__split_expr(fake);
	return 1;
}

int is_condition_call(struct expression *expr)
{
	struct expression *tmp;

	FOR_EACH_PTR_REVERSE(big_condition_stack, tmp) {
		if (expr == tmp || expr_get_parent_expr(expr) == tmp)
			return 1;
		if (tmp->pos.line < expr->pos.line)
			return 0;
	} END_FOR_EACH_PTR_REVERSE(tmp);

	return 0;
}

static bool gen_fake_function_assign(struct expression *expr)
{
	static struct expression *parsed;
	struct expression *assign, *parent;
	struct symbol *type;
	char buf[64];

	/* The rule is that every non-void function call has to be part of an
	 * assignment.  TODO:  Should we create a fake non-casted assignment
	 * for casted assignments?  Also faked assigns for += assignments?
	 */
	type = get_type(expr);
	if (!type || type == &void_ctype)
		return false;

	parent = get_parent_assignment(expr);
	if (parent && parent->type == EXPR_ASSIGNMENT)
		return false;

	parent = expr_get_fake_parent_expr(expr);
	if (parent) {
		struct expression *left = parent->left;

		if (parent == parsed)
			return false;
		if (!left || left->type != EXPR_SYMBOL)
			return false;
		if (strncmp(left->symbol_name->name, "__fake_assign_", 14) != 0)
			return false;
		parsed = parent;
		__split_expr(parent);
		return true;
	}

	// TODO: faked_assign skipping conditions is a hack
	if (is_condition_call(expr))
		return false;

	snprintf(buf, sizeof(buf), "__fake_assign_%p", expr);
	assign = create_fake_assign(buf, get_type(expr), expr);

	parsed = assign;
	__split_expr(assign);
	return true;
}

static void split_call(struct expression *expr)
{
	if (gen_fake_function_assign(expr))
		return;

	expr_set_parent_expr(expr->fn, expr);

	if (sym_name_is("__builtin_constant_p", expr->fn))
		return;
	if (handle__builtin_choose_expr(expr))
		return;
	__split_expr(expr->fn);
	split_args(expr);
	if (is_inline_func(expr->fn))
		add_inline_function(expr->fn->symbol->definition);
	if (inlinable(expr->fn))
		__inline_call = 1;
	__process_post_op_stack();
	__pass_to_client(expr, FUNCTION_CALL_HOOK_BEFORE);
	__pass_to_client(expr, FUNCTION_CALL_HOOK);
	__inline_call = 0;
	if (inlinable(expr->fn))
		parse_inline(expr);
	__pass_to_client(expr, CALL_HOOK_AFTER_INLINE);
	if (is_noreturn_func(expr->fn))
		nullify_path();
	if (!expr_get_parent_expr(expr) && indent_cnt == 1)
		__discard_fake_states(expr);
	handle_builtin_overflow_func(expr);
	__add_ptr_list((struct ptr_list **)&parsed_calls, expr);
}

static unsigned long skip_split;
void parse_assignment(struct expression *expr, bool shallow)
{
	struct expression *right;

	expr_set_parent_expr(expr->left, expr);
	expr_set_parent_expr(expr->right, expr);

	right = strip_expr(expr->right);
	if (!right)
		return;

	if (shallow)
		skip_split++;

	__pass_to_client(expr, RAW_ASSIGNMENT_HOOK);

	/* foo = !bar() */
	if (__handle_condition_assigns(expr))
		goto after_assign;
	/* foo = (x < 5 ? foo : 5); */
	if (__handle_select_assigns(expr))
		goto after_assign;
	/* foo = ({frob(); frob(); frob(); 1;}) */
	if (__handle_expr_statement_assigns(expr))
		goto done;  // FIXME: goto after
	/* foo = (3, 4); */
	if (handle_comma_assigns(expr))
		goto after_assign;
	if (handle__builtin_choose_expr_assigns(expr))
		goto after_assign;
	if (handle_postop_assigns(expr))
		goto done;  /* no need to goto after_assign */

	__split_expr(expr->right);
	if (outside_of_function())
		__pass_to_client(expr, GLOBAL_ASSIGNMENT_HOOK);
	else
		__pass_to_client(expr, ASSIGNMENT_HOOK);


	// FIXME: the ordering of this is tricky
	__fake_struct_member_assignments(expr);

	/* Re-examine ->right for inlines.  See the commit message */
	right = strip_expr(expr->right);
	if (expr->op == '=' && right->type == EXPR_CALL)
		__pass_to_client(expr, CALL_ASSIGNMENT_HOOK);

after_assign:
	if (get_macro_name(right->pos) &&
	    get_macro_name(expr->left->pos) != get_macro_name(right->pos))
		__pass_to_client(expr, MACRO_ASSIGNMENT_HOOK);

	__pass_to_client(expr, ASSIGNMENT_HOOK_AFTER);
	__split_expr(expr->left);

done:
	if (shallow)
		skip_split--;
}

static bool skip_split_off(struct expression *expr)
{
	if (expr->type == EXPR_CALL &&
	    sym_name_is("__smatch_stop_skip", expr->fn))
		return true;
	return false;
}

static bool is_no_parse_macro(struct expression *expr)
{
	static struct position prev_pos;
	static bool prev_ret;
	bool ret = false;
	char *macro;

	if (option_project != PROJ_KERNEL)
		return false;

	if (positions_eq(expr->pos, prev_pos))
		return prev_ret;
	macro = get_macro_name(expr->pos);
	if (!macro)
		goto done;
	if (strncmp(macro, "CHECK_PACKED_FIELDS_", 20) == 0)
		ret = true;
done:
	prev_ret = ret;
	prev_pos = expr->pos;
	return ret;
}

void __split_expr(struct expression *expr)
{
	if (!expr)
		return;

	if (skip_split_off(expr))
		__debug_skip = 0;
	if (__debug_skip)
		return;

	if (skip_split)
		return;

//	if (local_debug)
//		sm_msg("Debug expr_type %d %s expr = '%s'", expr->type, show_special(expr->op), expr_to_str(expr));

	if (__in_fake_assign && expr->type != EXPR_ASSIGNMENT)
		return;
	if (__in_fake_assign >= 4)  /* don't allow too much nesting */
		return;

	if (is_no_parse_macro(expr))
		return;

	push_expression(&big_expression_stack, expr);
	set_position(expr->pos);
	__pass_to_client(expr, EXPR_HOOK);

	switch (expr->type) {
	case EXPR_PREOP:
		expr_set_parent_expr(expr->unop, expr);

		__split_expr(expr->unop);
		if (expr->op == '*' &&
		    !prev_expression_is_getting_address(expr))
			__pass_to_client(expr, DEREF_HOOK);
		__pass_to_client(expr, OP_HOOK);
		break;
	case EXPR_POSTOP:
		expr_set_parent_expr(expr->unop, expr);

		__split_expr(expr->unop);
		push_expression(&post_op_stack, expr);
		break;
	case EXPR_STATEMENT:
		__expr_stmt_count++;
		stmt_set_parent_expr(expr->statement, expr);
		__split_stmt(expr->statement);
		__expr_stmt_count--;
		break;
	case EXPR_LOGICAL:
	case EXPR_COMPARE:
		expr_set_parent_expr(expr->left, expr);
		expr_set_parent_expr(expr->right, expr);

		__pass_to_client(expr, LOGIC_HOOK);
		__handle_logic(expr);
		break;
	case EXPR_BINOP:
		expr_set_parent_expr(expr->left, expr);
		expr_set_parent_expr(expr->right, expr);

		__pass_to_client(expr, BINOP_HOOK);
		__split_expr(expr->left);
		__split_expr(expr->right);
		break;
	case EXPR_COMMA:
		expr_set_parent_expr(expr->left, expr);
		expr_set_parent_expr(expr->right, expr);

		__split_expr(expr->left);
		__process_post_op_stack();
		__split_expr(expr->right);
		break;
	case EXPR_ASSIGNMENT:
		parse_assignment(expr, false);
		break;
	case EXPR_DEREF:
		expr_set_parent_expr(expr->deref, expr);

		__split_expr(expr->deref);
		__pass_to_client(expr, DEREF_HOOK);
		break;
	case EXPR_SLICE:
		expr_set_parent_expr(expr->base, expr);

		__split_expr(expr->base);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
		expr_set_parent_expr(expr->cast_expression, expr);

		__pass_to_client(expr, CAST_HOOK);
		__split_expr(expr->cast_expression);
		break;
	case EXPR_SIZEOF:
		if (expr->cast_expression)
			__pass_to_client(strip_parens(expr->cast_expression),
					 SIZEOF_HOOK);
		break;
	case EXPR_OFFSETOF:
	case EXPR_ALIGNOF:
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		expr_set_parent_expr(expr->conditional, expr);
		expr_set_parent_expr(expr->cond_true, expr);
		expr_set_parent_expr(expr->cond_false, expr);

		if (known_condition_true(expr->conditional)) {
			__split_expr(expr->cond_true);
			break;
		}
		if (known_condition_false(expr->conditional)) {
			__split_expr(expr->cond_false);
			break;
		}
		__pass_to_client(expr, SELECT_HOOK);
		__split_whole_condition(expr->conditional);
		__split_expr(expr->cond_true);
		__push_true_states();
		__use_false_states();
		__split_expr(expr->cond_false);
		__merge_true_states();
		break;
	case EXPR_CALL:
		split_call(expr);
		break;
	case EXPR_INITIALIZER:
		split_expr_list(expr->expr_list, expr);
		break;
	case EXPR_IDENTIFIER:
		expr_set_parent_expr(expr->ident_expression, expr);
		__split_expr(expr->ident_expression);
		break;
	case EXPR_INDEX:
		expr_set_parent_expr(expr->idx_expression, expr);
		__split_expr(expr->idx_expression);
		break;
	case EXPR_POS:
		expr_set_parent_expr(expr->init_expr, expr);
		__split_expr(expr->init_expr);
		break;
	case EXPR_SYMBOL:
		__pass_to_client(expr, SYM_HOOK);
		break;
	case EXPR_STRING:
		__pass_to_client(expr, STRING_HOOK);
		break;
	case EXPR_GENERIC: {
		struct expression *tmp;

		tmp = strip_Generic(expr);
		if (tmp != expr)
			__split_expr(tmp);
		break;
	}
	default:
		break;
	};
	__pass_to_client(expr, EXPR_HOOK_AFTER);
	pop_expression(&big_expression_stack);
}

static int is_forever_loop(struct statement *stmt)
{
	struct expression *expr;
	sval_t sval;

	expr = strip_expr(stmt->iterator_pre_condition);
	if (!expr)
		expr = stmt->iterator_post_condition;
	if (!expr) {
		/* this is a for(;;) loop... */
		return 1;
	}

	if (get_value(expr, &sval) && sval.value != 0)
		return 1;

	return 0;
}

static int loop_num;
static char *get_loop_name(int num)
{
	char buf[256];

	snprintf(buf, 255, "-loop%d", num);
	buf[255] = '\0';
	return alloc_sname(buf);
}

static struct bool_stmt_fn_list *once_through_hooks;
void add_once_through_hook(bool_stmt_func *fn)
{
	add_ptr_list(&once_through_hooks, fn);
}

static bool call_once_through_hooks(struct statement *stmt)
{
	bool_stmt_func *fn;

	if (option_assume_loops)
		return true;

	FOR_EACH_PTR(once_through_hooks, fn) {
		if ((fn)(stmt))
			return true;
	} END_FOR_EACH_PTR(fn);

	return false;
}

static void do_scope_hooks(void)
{
	struct position orig = current_pos;

	__call_scope_hooks();
	set_position(orig);
}

static const char *get_scoped_guard_label(struct statement *iterator)
{
	struct statement *stmt;
	bool found = false;
	int cnt = 0;

	if (!iterator || iterator->type != STMT_IF)
		return NULL;

	stmt = iterator->if_true;
	if (!stmt || stmt->type != STMT_COMPOUND)
		return NULL;

	FOR_EACH_PTR_REVERSE(stmt->stmts, stmt) {
		if (stmt->type == STMT_LABEL) {
			found = true;
			break;
		}
		if (++cnt > 2)
			break;
	} END_FOR_EACH_PTR_REVERSE(stmt);

	if (!found)
		return NULL;

	if (!stmt->label_identifier ||
	    stmt->label_identifier->type != SYM_LABEL ||
	    !stmt->label_identifier->ident)
		return NULL;
	return stmt->label_identifier->ident->name;
}

static bool is_scoped_guard_goto(struct statement *iterator, struct statement *post_stmt)
{
	struct statement *goto_stmt;
	struct expression *expr;
	const char *label;
	const char *goto_name;

	if (option_project != PROJ_KERNEL)
		return false;

	label = get_scoped_guard_label(iterator);
	if (!label)
		return false;

	if (!post_stmt || post_stmt->type != STMT_EXPRESSION)
		return false;

	expr = strip_expr(post_stmt->expression);
	if (expr->type != EXPR_PREOP ||
	    expr->op != '(' ||
	    expr->unop->type != EXPR_STATEMENT)
		return false;

	goto_stmt = expr->unop->statement;
	if (!goto_stmt || goto_stmt->type != STMT_COMPOUND)
		return false;
	goto_stmt = first_ptr_list((struct ptr_list *)goto_stmt->stmts);
	if (!goto_stmt ||
	    goto_stmt->type != STMT_GOTO ||
	    !goto_stmt->goto_label ||
	    goto_stmt->goto_label->type != SYM_LABEL ||
	    !goto_stmt->goto_label->ident)
		return false;
	goto_name = goto_stmt->goto_label->ident->name;
	if (!goto_name || strcmp(goto_name, label) != 0)
		return false;

	return true;
}

/*
 * Pre Loops are while and for loops.
 */
static void handle_pre_loop(struct statement *stmt)
{
	int once_through; /* we go through the loop at least once */
	struct sm_state *extra_sm = NULL;
	int unchanged = 0;
	char *loop_name;
	struct stree *stree = NULL;
	struct sm_state *sm = NULL;

	__push_scope_hooks();

	loop_name = get_loop_name(loop_num);
	loop_num++;

	split_declaration(stmt->iterator_syms);
	if (stmt->iterator_pre_statement) {
		__split_stmt(stmt->iterator_pre_statement);
		__prev_stmt = stmt->iterator_pre_statement;
	}

	loop_count++;
	__push_continues();
	__push_breaks();

	__merge_gotos(loop_name, NULL);

	__pass_to_client(stmt, PRELOOP_HOOK);

	extra_sm = __extra_handle_canonical_loops(stmt, &stree);
	__in_pre_condition++;
	__set_confidence_implied();
	__split_whole_condition_tf(stmt->iterator_pre_condition, &once_through);
	if (!stmt->iterator_pre_condition)
		once_through = true;
	__unset_confidence();
	if (once_through != true)
		once_through = call_once_through_hooks(stmt);
	__in_pre_condition--;
	FOR_EACH_SM(stree, sm) {
		set_state(sm->owner, sm->name, sm->sym, sm->state);
	} END_FOR_EACH_SM(sm);
	free_stree(&stree);
	if (extra_sm)
		extra_sm = get_sm_state(extra_sm->owner, extra_sm->name, extra_sm->sym);

	__split_stmt(stmt->iterator_statement);
	if (is_scoped_guard_goto(stmt->iterator_statement, stmt->iterator_post_statement)) {
		__merge_continues();
		__save_gotos(loop_name, NULL);
		if (once_through == true)
			__discard_false_states();
		else
			__merge_false_states();

		__pass_to_client(stmt, AFTER_LOOP_NO_BREAKS);
		__merge_breaks();
		goto done;
	}

	if (is_forever_loop(stmt)) {
		__merge_continues();
		__save_gotos(loop_name, NULL);

		__push_fake_cur_stree();
		__split_stmt(stmt->iterator_post_statement);
		stree = __pop_fake_cur_stree();

		__discard_false_states();
		__pass_to_client(stmt, AFTER_LOOP_NO_BREAKS);
		__use_breaks();

		if (!__path_is_null())
			__merge_stree_into_cur(stree);
		free_stree(&stree);
	} else {
		__merge_continues();
		unchanged = __iterator_unchanged(extra_sm);
		__split_stmt(stmt->iterator_post_statement);
		__prev_stmt = stmt->iterator_post_statement;
		__cur_stmt = stmt;

		__save_gotos(loop_name, NULL);
		__in_pre_condition++;
		__split_whole_condition(stmt->iterator_pre_condition);
		__in_pre_condition--;
		nullify_path();
		__merge_false_states();
		if (once_through == true)
			__discard_false_states();
		else
			__merge_false_states();

		if (extra_sm && unchanged)
			__extra_pre_loop_hook_after(extra_sm,
						stmt->iterator_post_statement,
						stmt->iterator_pre_condition);
		__pass_to_client(stmt, AFTER_LOOP_NO_BREAKS);
		__merge_breaks();
	}
done:
	loop_count--;

	do_scope_hooks();
}

/*
 * Post loops are do {} while();
 */
static void handle_post_loop(struct statement *stmt)
{
	char *loop_name;

	loop_name = get_loop_name(loop_num);
	loop_num++;
	loop_count++;

	__pass_to_client(stmt, POSTLOOP_HOOK);

	__push_continues();
	__push_breaks();
	__merge_gotos(loop_name, NULL);
	__split_stmt(stmt->iterator_statement);
	__merge_continues();
	if (!expr_is_zero(stmt->iterator_post_condition))
		__save_gotos(loop_name, NULL);

	if (is_forever_loop(stmt)) {
		__pass_to_client(stmt, AFTER_LOOP_NO_BREAKS);
		__use_breaks();
	} else {
		__split_whole_condition(stmt->iterator_post_condition);
		__use_false_states();
		__pass_to_client(stmt, AFTER_LOOP_NO_BREAKS);
		__merge_breaks();
	}
	loop_count--;
}

static int empty_statement(struct statement *stmt)
{
	if (!stmt)
		return 0;
	if (stmt->type == STMT_EXPRESSION && !stmt->expression)
		return 1;
	return 0;
}

static int last_stmt_on_same_line(void)
{
	struct statement *stmt;
	int i = 0;

	FOR_EACH_PTR_REVERSE(big_statement_stack, stmt) {
		if (!i++)
			continue;
		if  (stmt->pos.line == get_lineno())
			return 1;
		return 0;
	} END_FOR_EACH_PTR_REVERSE(stmt);
	return 0;
}

static void split_asm_ops(struct asm_operand_list *ops)
{
	struct asm_operand *op;

	FOR_EACH_PTR(ops, op) {
		__split_expr(op->expr);
	} END_FOR_EACH_PTR(op);
}

static int is_case_val(struct statement *stmt, sval_t sval)
{
	sval_t case_sval;

	if (stmt->type != STMT_CASE)
		return 0;
	if (!stmt->case_expression) {
		__set_default();
		return 1;
	}
	if (!get_value(stmt->case_expression, &case_sval))
		return 0;
	if (case_sval.value == sval.value)
		return 1;
	return 0;
}

static struct range_list *get_case_rl(struct expression *switch_expr,
				      struct expression *case_expr,
				      struct expression *case_to)
{
	sval_t start, end;
	struct range_list *rl = NULL;
	struct symbol *switch_type;

	switch_type = get_type(switch_expr);
	if (get_value(case_to, &end) && get_value(case_expr, &start)) {
		start = sval_cast(switch_type, start);
		end = sval_cast(switch_type, end);
		add_range(&rl, start, end);
	} else if (get_value(case_expr, &start)) {
		start = sval_cast(switch_type, start);
		add_range(&rl, start, start);
	}

	return rl;
}

static void split_known_switch(struct statement *stmt, sval_t sval)
{
	struct statement *tmp;
	struct range_list *rl;

	__split_expr(stmt->switch_expression);
	sval = sval_cast(get_type(stmt->switch_expression), sval);

	push_expression(&switch_expr_stack, stmt->switch_expression);
	__save_switch_states(top_expression(switch_expr_stack));
	nullify_path();
	__push_default();
	__push_breaks();

	stmt = stmt->switch_statement;

	__push_scope_hooks();
	FOR_EACH_PTR(stmt->stmts, tmp) {
		__smatch_lineno = tmp->pos.line;
		// FIXME: what if default comes before the known case statement?
		if (is_case_val(tmp, sval)) {
			rl = alloc_rl(sval, sval);
			__merge_switches(top_expression(switch_expr_stack), rl);
			__pass_case_to_client(top_expression(switch_expr_stack), rl);
			stmt_set_parent_stmt(tmp->case_statement, tmp);
			__split_stmt(tmp->case_statement);
			goto next;
		}
		if (__path_is_null())
			continue;
		__split_stmt(tmp);
next:
		if (__path_is_null()) {
			__set_default();
			goto out;
		}
	} END_FOR_EACH_PTR(tmp);
out:
	do_scope_hooks();
	if (!__pop_default())
		__merge_switches(top_expression(switch_expr_stack), NULL);
	__discard_switches();
	__merge_breaks();
	pop_expression(&switch_expr_stack);
}

static void split_case(struct statement *stmt)
{
	struct range_list *rl = NULL;

	expr_set_parent_stmt(stmt->case_expression, stmt);
	expr_set_parent_stmt(stmt->case_to, stmt);

	rl = get_case_rl(top_expression(switch_expr_stack),
			 stmt->case_expression, stmt->case_to);
	while (stmt->case_statement->type == STMT_CASE) {
		struct range_list *tmp;

		tmp = get_case_rl(top_expression(switch_expr_stack),
				  stmt->case_statement->case_expression,
				  stmt->case_statement->case_to);
		if (!tmp)
			goto next;
		rl = rl_union(rl, tmp);
		if (!stmt->case_expression)
			__set_default();
next:
		stmt = stmt->case_statement;
	}

	__merge_switches(top_expression(switch_expr_stack), rl);

	if (!stmt->case_expression)
		__set_default();

	stmt_set_parent_stmt(stmt->case_statement, stmt);
	__split_stmt(stmt->case_statement);
}

int time_parsing_function(void)
{
	return ms_since(&fn_start_time) / 1000;
}

bool taking_too_long(void)
{
	if ((ms_since(&outer_fn_start_time) / 1000) > 60 * 5) /* five minutes */
		return 1;
	return 0;
}

struct statement *get_last_stmt(void)
{
	struct symbol *fn;
	struct statement *stmt;

	fn = get_base_type(cur_func_sym);
	if (!fn)
		return NULL;
	stmt = fn->stmt;
	if (!stmt)
		stmt = fn->inline_stmt;
	if (!stmt || stmt->type != STMT_COMPOUND)
		return NULL;
	stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
	if (stmt && stmt->type == STMT_LABEL)
		stmt = stmt->label_statement;
	return stmt;
}

int is_last_stmt(struct statement *cur_stmt)
{
	struct statement *last;

	last = get_last_stmt();
	if (last && last == cur_stmt)
		return 1;
	return 0;
}

static bool is_function_scope(struct statement *stmt)
{
	struct symbol *base_type;

	if (!cur_func_sym)
		return false;

	base_type = get_base_type(cur_func_sym);
	if (base_type->stmt == stmt ||
	    base_type->inline_stmt == stmt)
		return true;

	return false;
}

/*
 * Sometimes people do a little backwards goto as the last statement in a
 * function.
 *
 * exit:
 *	return ret;
 * free:
 *	kfree(foo);
 *	goto exit;
 *
 * Smatch generally does a hacky thing where it just parses the code one
 * time from top to bottom, but in this case we need to go backwards so that
 * we record what "return ret;" returns.
 *
 */
static void handle_backward_goto_at_end(struct statement *goto_stmt)
{
	const char *goto_name, *label_name;
	struct statement *func_stmt;
	struct symbol *base_type = get_base_type(cur_func_sym);
	struct statement *tmp;
	int found = 0;

	if (!is_last_stmt(goto_stmt))
		return;
	if (last_goto_statement_handled)
		return;
	last_goto_statement_handled = 1;

	if (!goto_stmt->goto_label ||
	    goto_stmt->goto_label->type != SYM_LABEL ||
	    !goto_stmt->goto_label->ident)
		return;
	goto_name = goto_stmt->goto_label->ident->name;

	func_stmt = base_type->stmt;
	if (!func_stmt)
		func_stmt = base_type->inline_stmt;
	if (!func_stmt)
		return;
	if (func_stmt->type != STMT_COMPOUND)
		return;

	FOR_EACH_PTR(func_stmt->stmts, tmp) {
		if (!found) {
			if (tmp->type != STMT_LABEL)
				continue;
			if (!tmp->label_identifier ||
			    tmp->label_identifier->type != SYM_LABEL ||
			    !tmp->label_identifier->ident)
				continue;
			label_name = tmp->label_identifier->ident->name;
			if (strcmp(goto_name, label_name) != 0)
				continue;
			found = 1;
			__reparsing_code = true;
		}
		__split_stmt(tmp);
	} END_FOR_EACH_PTR(tmp);
	__reparsing_code = false;
}

static void fake_a_return(void)
{
	struct expression *ret = NULL;

	nullify_path();
	__unnullify_path();

	if (cur_func_return_type() != &void_ctype)
		ret = unknown_value_expression(NULL);

	__pass_to_client(ret, RETURN_HOOK);
	nullify_path();
}

static void split_ret_value(struct expression *expr)
{
	struct symbol *type;

	if (!expr)
		return;

	type = get_real_base_type(cur_func_sym);
	type = get_real_base_type(type);
	expr = fake_a_variable_assign(type, NULL, expr, -1);

	__in_fake_var_assign++;
	__split_expr(expr);
	__in_fake_var_assign--;
}

static void fake_an_empty_default(struct position pos)
{
	static struct statement none = {};

	none.pos = pos;
	none.type = STMT_NONE;
	__merge_switches(top_expression(switch_expr_stack), NULL);
	__split_stmt(&none);
}

static void split_compound(struct statement *stmt)
{
	struct statement *prev = NULL;
	struct statement *cur = NULL;
	struct statement *next;

	__push_scope_hooks();

	FOR_EACH_PTR(stmt->stmts, next) {
		/* just set them all ahead of time */
		stmt_set_parent_stmt(next, stmt);

		if (cur) {
			__prev_stmt = prev;
			__next_stmt = next;
			__cur_stmt = cur;
			__split_stmt(cur);
		}
		prev = cur;
		cur = next;
	} END_FOR_EACH_PTR(next);
	if (cur) {
		__prev_stmt = prev;
		__cur_stmt = cur;
		__next_stmt = NULL;
		__split_stmt(cur);
	}

	/*
	 * For function scope, then delay calling the scope hooks until the
	 * end of function hooks can run.
	 */
	if (!is_function_scope(stmt))
		do_scope_hooks();
}

void __split_label_stmt(struct statement *stmt)
{
	if (stmt->label_identifier &&
	    stmt->label_identifier->type == SYM_LABEL &&
	    stmt->label_identifier->ident) {
		loop_count |= 0x0800000;
		__merge_gotos(stmt->label_identifier->ident->name, stmt->label_identifier);
	}
}

static void find_asm_gotos(struct statement *stmt)
{
	struct symbol *sym;

	FOR_EACH_PTR(stmt->asm_labels, sym) {
		__save_gotos(sym->ident->name, sym);
	} END_FOR_EACH_PTR(sym);
}

static void split_if_statement(struct statement *stmt)
{
	int known_tf;

	stmt_set_parent_stmt(stmt->if_true, stmt);
	stmt_set_parent_stmt(stmt->if_false, stmt);
	expr_set_parent_stmt(stmt->if_conditional, stmt);

	if (empty_statement(stmt->if_true) &&
		last_stmt_on_same_line() &&
		!get_macro_name(stmt->if_true->pos))
		sm_warning("if();");

	__split_whole_condition_tf(stmt->if_conditional, &known_tf);
	if (known_tf == true) {
		__split_stmt(stmt->if_true);
		__discard_false_states();
		return;
	} else if (known_tf == false) {
		__use_false_states();
		__split_stmt(stmt->if_false);
		return;
	}

	__split_stmt(stmt->if_true);
	__push_true_states();
	__use_false_states();
	__split_stmt(stmt->if_false);
	__merge_true_states();
}

static bool already_parsed_call(struct expression *call)
{
	struct expression *expr;

	FOR_EACH_PTR(parsed_calls, expr) {
		if (expr == call)
			return true;
	} END_FOR_EACH_PTR(expr);
	return false;
}

static void free_parsed_call_stuff(bool free_fake_states)
{
	free_expression_stack(&parsed_calls);
	if (free_fake_states)
		__discard_fake_states(NULL);
}

void __split_stmt(struct statement *stmt)
{
	sval_t sval;
	struct timeval start, stop;
	bool skip_after = false;

	gettimeofday(&start, NULL);

	if (!stmt)
		goto out;

	if (!__in_fake_assign)
		__silence_warnings_for_stmt = false;

	if (__bail_on_rest_of_function || is_skipped_function())
		return;

	if (out_of_memory() || taking_too_long()) {
		gettimeofday(&start, NULL);

		__bail_on_rest_of_function = 1;
		final_pass = 1;
		if (option_spammy)
			sm_perror("Function too hairy.  Giving up. %lu seconds",
			       start.tv_sec - fn_start_time.tv_sec);
		fake_a_return();
		final_pass = 0;  /* turn off sm_msg() from here */
		return;
	}

	indent_cnt++;

	add_ptr_list(&big_statement_stack, stmt);
	free_expression_stack(&big_expression_stack);
	free_parsed_call_stuff(indent_cnt == 1);
	set_position(stmt->pos);
	__pass_to_client(stmt, STMT_HOOK);

	switch (stmt->type) {
	case STMT_DECLARATION:
		split_declaration(stmt->declaration);
		break;
	case STMT_RETURN:
		expr_set_parent_stmt(stmt->ret_value, stmt);

		split_ret_value(stmt->ret_value);
		__process_post_op_stack();
		__call_all_scope_hooks();
		__pass_to_client(stmt->ret_value, RETURN_HOOK);
		nullify_path();
		break;
	case STMT_EXPRESSION:
		expr_set_parent_stmt(stmt->expression, stmt);
		expr_set_parent_stmt(stmt->context, stmt);

		__split_expr(stmt->expression);
		break;
	case STMT_COMPOUND:
		split_compound(stmt);
		break;
	case STMT_IF:
		split_if_statement(stmt);
		break;
	case STMT_ITERATOR:
		stmt_set_parent_stmt(stmt->iterator_pre_statement, stmt);
		stmt_set_parent_stmt(stmt->iterator_statement, stmt);
		stmt_set_parent_stmt(stmt->iterator_post_statement, stmt);
		expr_set_parent_stmt(stmt->iterator_pre_condition, stmt);
		expr_set_parent_stmt(stmt->iterator_post_condition, stmt);

		if (stmt->iterator_pre_condition)
			handle_pre_loop(stmt);
		else if (stmt->iterator_post_condition)
			handle_post_loop(stmt);
		else {
			// these are for(;;) type loops.
			handle_pre_loop(stmt);
		}
		break;
	case STMT_SWITCH:
		stmt_set_parent_stmt(stmt->switch_statement, stmt);
		expr_set_parent_stmt(stmt->switch_expression, stmt);

		if (get_value(stmt->switch_expression, &sval)) {
			split_known_switch(stmt, sval);
			break;
		}
		__split_expr(stmt->switch_expression);
		push_expression(&switch_expr_stack, stmt->switch_expression);
		__save_switch_states(top_expression(switch_expr_stack));
		nullify_path();
		__push_default();
		__push_breaks();
		__split_stmt(stmt->switch_statement);
		if (!__pop_default() && have_remaining_cases())
			fake_an_empty_default(stmt->pos);
		__discard_switches();
		__merge_breaks();
		pop_expression(&switch_expr_stack);
		break;
	case STMT_CASE:
		split_case(stmt);
		break;
	case STMT_LABEL:
		__split_label_stmt(stmt);
		__pass_to_client(stmt, STMT_HOOK_AFTER);
		skip_after = true;
		__split_stmt(stmt->label_statement);
		break;
	case STMT_GOTO:
		expr_set_parent_stmt(stmt->goto_expression, stmt);

		__split_expr(stmt->goto_expression);
		if (stmt->goto_label && stmt->goto_label->type == SYM_NODE) {
			if (!strcmp(stmt->goto_label->ident->name, "break")) {
				__process_breaks();
			} else if (!strcmp(stmt->goto_label->ident->name,
					   "continue")) {
				__process_continues();
			}
		} else if (stmt->goto_label &&
			   stmt->goto_label->type == SYM_LABEL &&
			   stmt->goto_label->ident) {
			__save_gotos(stmt->goto_label->ident->name, stmt->goto_label);
		}
		handle_backward_goto_at_end(stmt);
		nullify_path();
		break;
	case STMT_NONE:
		break;
	case STMT_ASM:
		expr_set_parent_stmt(stmt->asm_string, stmt);

		find_asm_gotos(stmt);
		__pass_to_client(stmt, ASM_HOOK);
		__split_expr(stmt->asm_string);
		split_asm_ops(stmt->asm_outputs);
		split_asm_ops(stmt->asm_inputs);
		split_expr_list(stmt->asm_clobbers, NULL);
		break;
	case STMT_CONTEXT:
		break;
	case STMT_RANGE:
		__split_expr(stmt->range_expression);
		__split_expr(stmt->range_low);
		__split_expr(stmt->range_high);
		break;
	}
	if (!skip_after)
		__pass_to_client(stmt, STMT_HOOK_AFTER);
	if (--indent_cnt == 1)
		free_parsed_call_stuff(true);

out:
	__process_post_op_stack();

	gettimeofday(&stop, NULL);
	if (option_time_stmt && stmt)
		sm_msg("stmt_time%s: %ld",
		       stmt->type == STMT_COMPOUND ? "_block" : "",
		       stop.tv_sec - start.tv_sec);
}

static void split_expr_list(struct expression_list *expr_list, struct expression *parent)
{
	struct expression *expr;

	FOR_EACH_PTR(expr_list, expr) {
		expr_set_parent_expr(expr, parent);
		__split_expr(expr);
		__process_post_op_stack();
	} END_FOR_EACH_PTR(expr);
}

static bool cast_arg(struct symbol *type, struct expression *arg)
{
	struct symbol *orig;

	if (!type)
		return false;

	arg = strip_parens(arg);
	if (arg != strip_expr(arg))
		return true;

	orig = get_type(arg);
	if (!orig)
		return true;
	if (types_equiv(orig, type))
		return false;

	if (orig->type == SYM_ARRAY && type->type == SYM_PTR)
		return true;

	/*
	 * I would have expected that we could just do use (orig == type) but I
	 * guess for pointers we need to get the basetype to do that comparison.
	 *
	 */

	if (orig->type != SYM_PTR ||
	    type->type != SYM_PTR) {
		if (type_fits(type, orig))
			return false;
		return true;
	}
	orig = get_real_base_type(orig);
	type = get_real_base_type(type);
	if (orig == type)
		return false;

	return true;
}

static struct expression *fake_a_variable_assign(struct symbol *type, struct expression *call, struct expression *expr, int nr)
{
	char buf[64];
	bool cast;

	if (!expr || !cur_func_sym)
		return NULL;

	if (already_parsed_call(call))
		return NULL;

	if (expr->type == EXPR_ASSIGNMENT)
		return expr;

	/* for va_args then we don't know the type */
	if (!type)
		type = get_type(expr);

	cast = cast_arg(type, expr);
	/*
	 * Using expr_to_sym() here is a hack.  We want to say that we don't
	 * need to assign frob(foo) or frob(foo->bar) if the types are right.
	 * It turns out faking these assignments is way more expensive than I
	 * would have imagined.  I'm not sure why exactly.
	 *
	 */
	if (!cast) {
		/*
		 * if the code is "return *p;" where "p" is a user pointer then
		 * we want to create a fake assignment so that it sets the state
		 * in check_kernel_user_data.c.
		 *
		 */
		if (expr->type != EXPR_PREOP &&
		    expr->op != '*' && expr->op != '&' &&
		    expr_to_sym(expr))
			return expr;
	}

	if (nr == -1)
		snprintf(buf, sizeof(buf), "__fake_return_%p", expr);
	else
		snprintf(buf, sizeof(buf), "__fake_param_%p_%d", call, nr);

	return create_fake_assign(buf, type, expr);
}

struct expression *get_fake_return_variable(struct expression *expr)
{
	struct expression *tmp;

	tmp = expr_get_fake_parent_expr(expr);
	if (!tmp || tmp->type != EXPR_ASSIGNMENT)
		return NULL;

	return tmp->left;
}

static void split_args(struct expression *expr)
{
	struct expression *arg, *tmp;
	struct symbol *type;
	int i;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		expr_set_parent_expr(arg, expr);
		type = get_arg_type(expr->fn, i);
		tmp = fake_a_variable_assign(type, expr, arg, i);
		if (tmp != arg)
			__in_fake_var_assign++;
		__split_expr(tmp);
		if (tmp != arg)
			__in_fake_var_assign--;
		__process_post_op_stack();
	} END_FOR_EACH_PTR(arg);
}

static void call_cleanup_fn(void *_sym)
{
	struct symbol *sym = _sym;
	struct expression *call, *arg;
	struct expression_list *args = NULL;
	struct position orig = current_pos;

	arg = symbol_expression(sym);
	arg = preop_expression(arg, '&');
	add_ptr_list(&args, arg);
	call = call_expression(sym->cleanup, args);

	__split_expr(call);
	set_position(orig);
}

static void add_cleanup_hook(struct symbol *sym)
{
	if (!sym->cleanup)
		return;
	add_scope_hook(&call_cleanup_fn, sym);
}

static void split_sym(struct symbol *sym)
{
	if (!sym)
		return;
	if (!(sym->namespace & NS_SYMBOL))
		return;

	__split_stmt(sym->stmt);
	__split_expr(sym->array_size);
	if (sym->cleanup)
		add_cleanup_hook(sym);
	split_symlist(sym->arguments);
	split_symlist(sym->symbol_list);
	__split_stmt(sym->inline_stmt);
	split_symlist(sym->inline_symbol_list);
}

static void split_symlist(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		split_sym(sym);
	} END_FOR_EACH_PTR(sym);
}

typedef void (fake_cb)(struct expression *expr);

static int member_to_number(struct expression *expr, struct ident *member)
{
	struct symbol *type, *tmp;
	char *name;
	int i;

	if (!member)
		return -1;
	name = member->name;

	type = get_type(expr);
	if (!type || type->type != SYM_STRUCT)
		return -1;

	i = -1;
	FOR_EACH_PTR(type->symbol_list, tmp) {
		i++;
		if (!tmp->ident)
			continue;
		if (strcmp(name, tmp->ident->name) == 0)
			return i;
	} END_FOR_EACH_PTR(tmp);
	return -1;
}

static struct ident *number_to_member(struct expression *expr, int num)
{
	struct symbol *type, *member;
	int i = 0;

	type = get_type(expr);
	if (!type || type->type != SYM_STRUCT)
		return NULL;

	FOR_EACH_PTR(type->symbol_list, member) {
		if (i == num)
			return member->ident;
		i++;
	} END_FOR_EACH_PTR(member);
	return NULL;
}

static void fake_element_assigns_helper(struct expression *array, struct expression_list *expr_list, fake_cb *fake_cb);

static void set_inner_struct_members(struct expression *expr, struct symbol *member)
{
	struct expression *edge_member, *assign;
	struct symbol *base = get_real_base_type(member);
	struct symbol *tmp;

	if (member->ident)
		expr = member_expression(expr, '.', member->ident);

	FOR_EACH_PTR(base->symbol_list, tmp) {
		struct symbol *type;

		type = get_real_base_type(tmp);
		if (!type)
			continue;

		edge_member = member_expression(expr, '.', tmp->ident);
		if (get_extra_state(edge_member))
			continue;

		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			set_inner_struct_members(expr, tmp);
			continue;
		}

		if (!tmp->ident)
			continue;

		assign = assign_expression(edge_member, '=', zero_expr());
		__split_expr(assign);
	} END_FOR_EACH_PTR(tmp);


}

static void set_unset_to_zero(struct symbol *type, struct expression *expr)
{
	struct symbol *tmp;
	struct expression *member = NULL;
	struct expression *assign;

	FOR_EACH_PTR(type->symbol_list, tmp) {
		type = get_real_base_type(tmp);
		if (!type)
			continue;

		if (tmp->ident) {
			member = member_expression(expr, '.', tmp->ident);
			if (get_extra_state(member))
				continue;
		}

		if (type->type == SYM_UNION || type->type == SYM_STRUCT) {
			set_inner_struct_members(expr, tmp);
			continue;
		}
		if (type->type == SYM_ARRAY)
			continue;
		if (!tmp->ident)
			continue;

		assign = assign_expression(member, '=', zero_expr());
		__split_expr(assign);
	} END_FOR_EACH_PTR(tmp);
}

static void fake_member_assigns_helper(struct expression *symbol, struct expression_list *members, fake_cb *fake_cb)
{
	struct expression *deref, *assign, *tmp, *right;
	struct symbol *struct_type, *type;
	struct ident *member;
	int member_idx;

	struct_type = get_type(symbol);
	if (!struct_type ||
	    (struct_type->type != SYM_STRUCT && struct_type->type != SYM_UNION))
		return;

	/*
	 * We're parsing an initializer that could look something like this:
	 * struct foo foo = {
	 *	42,
	 *	.whatever.xxx = 11,
	 *	.zzz = 12,
	 * };
	 *
	 * So what we have here is a list with 42, .whatever, and .zzz.  We need
	 * to break it up into left and right sides of the assignments.
	 *
	 */
	member_idx = 0;
	FOR_EACH_PTR(members, tmp) {
		deref = NULL;
		if (tmp->type == EXPR_IDENTIFIER) {
			member_idx = member_to_number(symbol, tmp->expr_ident);
			while (tmp->type == EXPR_IDENTIFIER) {
				member = tmp->expr_ident;
				tmp = tmp->ident_expression;
				if (deref)
					deref = member_expression(deref, '.', member);
				else
					deref = member_expression(symbol, '.', member);
			}
		} else {
			member = number_to_member(symbol, member_idx);
			deref = member_expression(symbol, '.', member);
		}
		right = tmp;
		member_idx++;
		if (right->type == EXPR_INITIALIZER) {
			type = get_type(deref);
			if (type && type->type == SYM_ARRAY)
				fake_element_assigns_helper(deref, right->expr_list, fake_cb);
			else
				fake_member_assigns_helper(deref, right->expr_list, fake_cb);
		} else {
			assign = assign_expression(deref, '=', right);
			fake_cb(assign);
		}
	} END_FOR_EACH_PTR(tmp);

	set_unset_to_zero(struct_type, symbol);
}

static void fake_member_assigns(struct symbol *sym, fake_cb *fake_cb)
{
	fake_member_assigns_helper(symbol_expression(sym),
				   sym->initializer->expr_list, fake_cb);
}

static void fake_element_assigns_helper(struct expression *array, struct expression_list *expr_list, fake_cb *fake_cb)
{
	struct expression *offset, *binop, *assign, *tmp;
	struct symbol *type;
	int idx, max;

	if (ptr_list_size((struct ptr_list *)expr_list) > 256) {
		binop = array_element_expression(array, unknown_value_expression(array));
		assign = assign_expression(binop, '=', unknown_value_expression(array));
		/* fake_cb is probably call_global_assign_hooks() which is a
		 * a kind of hacky short cut because it's too expensive to deal
		 * with huge global arrays.  However, we may as well parse this
		 * one the long way seeing as it's a one time thing.
		 */
		__split_expr(assign);
		return;
	}

	max = 0;
	idx = 0;
	FOR_EACH_PTR(expr_list, tmp) {
		if (tmp->type == EXPR_INDEX) {
			if (tmp->idx_from != tmp->idx_to)
				return;
			idx = tmp->idx_from;
			if (idx > max)
				max = idx;
			if (!tmp->idx_expression)
				goto next;
			tmp = tmp->idx_expression;
		}
		offset = value_expr(idx);
		binop = array_element_expression(array, offset);
		if (tmp->type == EXPR_INITIALIZER) {
			type = get_type(binop);
			if (type && type->type == SYM_ARRAY)
				fake_element_assigns_helper(binop, tmp->expr_list, fake_cb);
			else
				fake_member_assigns_helper(binop, tmp->expr_list, fake_cb);
		} else {
			assign = assign_expression(binop, '=', tmp);
			fake_cb(assign);
		}
next:
		idx++;
		if (idx > max)
			max = idx;
	} END_FOR_EACH_PTR(tmp);

	__call_array_initialized_hooks(array, max);
}

static void fake_element_assigns(struct symbol *sym, fake_cb *fake_cb)
{
	fake_element_assigns_helper(symbol_expression(sym), sym->initializer->expr_list, fake_cb);
}

static void fake_assign_expr(struct symbol *sym)
{
	struct expression *assign, *symbol;

	symbol = symbol_expression(sym);
	assign = assign_expression(symbol, '=', sym->initializer);
	__split_expr(assign);
}

static void do_initializer_stuff(struct symbol *sym)
{
	if (!sym->initializer)
		return;

	if (sym->initializer->type == EXPR_INITIALIZER) {
		if (get_real_base_type(sym)->type == SYM_ARRAY) {
			__in_array_initializer++;
			fake_element_assigns(sym, __split_expr);
			__in_array_initializer--;
		} else {
			fake_member_assigns(sym, __split_expr);
		}
	} else {
		fake_assign_expr(sym);
	}
}

static void split_declaration(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		__pass_to_client(sym, DECLARATION_HOOK);
		do_initializer_stuff(sym);
		__pass_to_client(sym, DECLARATION_HOOK_AFTER);
		split_sym(sym);
	} END_FOR_EACH_PTR(sym);
}

static void call_global_assign_hooks(struct expression *assign)
{
	__pass_to_client(assign, GLOBAL_ASSIGNMENT_HOOK);
}

static void fake_global_assign(struct symbol *sym)
{
	struct expression *assign, *symbol;

	if (get_real_base_type(sym)->type == SYM_ARRAY) {
		if (sym->initializer && sym->initializer->type == EXPR_INITIALIZER) {
			fake_element_assigns(sym, call_global_assign_hooks);
		} else if (sym->initializer) {
			symbol = symbol_expression(sym);
			assign = assign_expression(symbol, '=', sym->initializer);
			__pass_to_client(assign, GLOBAL_ASSIGNMENT_HOOK);
		} else {
			fake_element_assigns_helper(symbol_expression(sym), NULL, call_global_assign_hooks);
		}
	} else if (get_real_base_type(sym)->type == SYM_STRUCT) {
		if (sym->initializer && sym->initializer->type == EXPR_INITIALIZER) {
			fake_member_assigns(sym, call_global_assign_hooks);
		} else if (sym->initializer) {
			symbol = symbol_expression(sym);
			assign = assign_expression(symbol, '=', sym->initializer);
			__pass_to_client(assign, GLOBAL_ASSIGNMENT_HOOK);
		} else {
			fake_member_assigns_helper(symbol_expression(sym), NULL, call_global_assign_hooks);
		}
	} else {
		symbol = symbol_expression(sym);
		if (sym->initializer) {
			assign = assign_expression(symbol, '=', sym->initializer);
			__split_expr(assign);
		} else {
			assign = assign_expression(symbol, '=', zero_expr());
		}
		__pass_to_client(assign, GLOBAL_ASSIGNMENT_HOOK);
	}
}

static void start_function_definition(struct symbol *sym)
{
	__in_function_def = 1;
	__pass_to_client(sym, FUNC_DEF_HOOK);
	__in_function_def = 0;
	__pass_to_client(sym, AFTER_DEF_HOOK);

}

static void parse_fn_statements(struct symbol *base)
{
	__split_stmt(base->stmt);
	__split_stmt(base->inline_stmt);
}

void add_function_data(unsigned long *fn_data)
{
	__add_ptr_list(&fn_data_list, fn_data);
}

static void clear_function_data(void)
{
	unsigned long *tmp;

	FOR_EACH_PTR(fn_data_list, tmp) {
		*tmp = 0;
	} END_FOR_EACH_PTR(tmp);
}

static void record_func_time(void)
{
	struct timeval stop;
	int func_time;
	char buf[32];

	gettimeofday(&stop, NULL);
	func_time = stop.tv_sec - fn_start_time.tv_sec;
	snprintf(buf, sizeof(buf), "%d", func_time);
	sql_insert_return_implies(FUNC_TIME, 0, "", buf);
	if (option_time && func_time > 2) {
		final_pass++;
		sm_msg("func_time: %d", func_time);
		final_pass--;
	}
}

static void split_function(struct symbol *sym)
{
	struct symbol *base_type = get_base_type(sym);

	if (!base_type->stmt && !base_type->inline_stmt)
		return;

	gettimeofday(&outer_fn_start_time, NULL);
	gettimeofday(&fn_start_time, NULL);
	cur_func_sym = sym;
	if (sym->ident)
		cur_func = sym->ident->name;
	if (option_process_function && cur_func &&
	    strcmp(option_process_function, cur_func) != 0)
		return;
	set_position(sym->pos);
	clear_function_data();
	loop_count = 0;
	last_goto_statement_handled = 0;
	sm_debug("new function:  %s\n", cur_func);
	__stree_id = 0;
	if (option_two_passes) {
		__unnullify_path();
		loop_num = 0;
		final_pass = 0;
		start_function_definition(sym);
		parse_fn_statements(base_type);
		do_scope_hooks();
		nullify_path();
	}
	__unnullify_path();
	loop_num = 0;
	final_pass = 1;
	start_function_definition(sym);
	parse_fn_statements(base_type);
	if (!__path_is_null() &&
	    cur_func_return_type() == &void_ctype &&
	    !__bail_on_rest_of_function) {
		__call_all_scope_hooks();
		__pass_to_client(NULL, RETURN_HOOK);
		nullify_path();
	}
	__pass_to_client(sym, END_FUNC_HOOK);
	__free_scope_hooks();
	__pass_to_client(sym, AFTER_FUNC_HOOK);
	sym->parsed = true;

	clear_all_states();

	record_func_time();

	cur_func_sym = NULL;
	cur_func = NULL;
	free_data_info_allocs();
	free_expression_stack(&switch_expr_stack);
	__free_ptr_list((struct ptr_list **)&big_statement_stack);
	__bail_on_rest_of_function = 0;
}

static void save_flow_state(void)
{
	unsigned long *tmp;

	__add_ptr_list(&backup, INT_PTR(loop_num << 2));
	__add_ptr_list(&backup, INT_PTR(loop_count << 2));
	__add_ptr_list(&backup, INT_PTR(final_pass << 2));

	__add_ptr_list(&backup, big_statement_stack);
	__add_ptr_list(&backup, big_expression_stack);
	__add_ptr_list(&backup, big_condition_stack);
	__add_ptr_list(&backup, switch_expr_stack);

	__add_ptr_list(&backup, cur_func_sym);

	__add_ptr_list(&backup, parsed_calls);

	__add_ptr_list(&backup, __prev_stmt);
	__add_ptr_list(&backup, __cur_stmt);
	__add_ptr_list(&backup, __next_stmt);

	FOR_EACH_PTR(fn_data_list, tmp) {
		__add_ptr_list(&backup, (void *)*tmp);
	} END_FOR_EACH_PTR(tmp);
}

static void *pop_backup(void)
{
	void *ret;

	ret = last_ptr_list(backup);
	delete_ptr_list_last(&backup);
	return ret;
}

static void restore_flow_state(void)
{
	unsigned long *tmp;

	FOR_EACH_PTR_REVERSE(fn_data_list, tmp) {
		*tmp = (unsigned long)pop_backup();
	} END_FOR_EACH_PTR_REVERSE(tmp);

	__next_stmt = pop_backup();
	__cur_stmt = pop_backup();
	__prev_stmt = pop_backup();

	parsed_calls = pop_backup();

	cur_func_sym = pop_backup();
	switch_expr_stack = pop_backup();
	big_condition_stack = pop_backup();
	big_expression_stack = pop_backup();
	big_statement_stack = pop_backup();
	final_pass = PTR_INT(pop_backup()) >> 2;
	loop_count = PTR_INT(pop_backup()) >> 2;
	loop_num = PTR_INT(pop_backup()) >> 2;
}

void parse_inline(struct expression *call)
{
	struct symbol *base_type;
	char *cur_func_bak = cur_func;  /* not aligned correctly for backup */
	struct timeval time_backup = fn_start_time;
	struct expression *orig_inline = __inline_fn;
	int orig_budget;

	if (out_of_memory() || taking_too_long())
		return;

	if (already_parsed_call(call))
		return;

	save_flow_state();

	gettimeofday(&fn_start_time, NULL);
	__pass_to_client(call, INLINE_FN_START);
	final_pass = 0;  /* don't print anything */
	__inline_fn = call;
	orig_budget = inline_budget;
	inline_budget = inline_budget - 5;

	base_type = get_base_type(call->fn->symbol);
	cur_func_sym = call->fn->symbol;
	if (call->fn->symbol->ident)
		cur_func = call->fn->symbol->ident->name;
	else
		cur_func = NULL;
	set_position(call->fn->symbol->pos);

	save_all_states();
	big_statement_stack = NULL;
	big_expression_stack = NULL;
	big_condition_stack = NULL;
	switch_expr_stack = NULL;
	parsed_calls = NULL;

	sm_debug("inline function:  %s\n", cur_func);
	__unnullify_path();
	clear_function_data();
	loop_num = 0;
	loop_count = 0;
	start_function_definition(call->fn->symbol);
	parse_fn_statements(base_type);
	if (!__path_is_null() &&
	    cur_func_return_type() == &void_ctype &&
	    !__bail_on_rest_of_function) {
		__call_all_scope_hooks();
		__pass_to_client(NULL, RETURN_HOOK);
		nullify_path();
	}
	__pass_to_client(call->fn->symbol, END_FUNC_HOOK);
	__free_scope_hooks();
	__pass_to_client(call->fn->symbol, AFTER_FUNC_HOOK);
	call->fn->symbol->parsed = true;

	free_expression_stack(&switch_expr_stack);
	__free_ptr_list((struct ptr_list **)&big_statement_stack);
	nullify_path();
	free_goto_stack();

	record_func_time();

	restore_flow_state();
	fn_start_time = time_backup;
	cur_func = cur_func_bak;

	restore_all_states();
	set_position(call->pos);
	__inline_fn = orig_inline;
	inline_budget = orig_budget;
	__pass_to_client(call, INLINE_FN_END);
}

static struct symbol_list *inlines_called;
static void add_inline_function(struct symbol *sym)
{
	static struct symbol_list *already_added;
	struct symbol *tmp;

	FOR_EACH_PTR(already_added, tmp) {
		if (tmp == sym)
			return;
	} END_FOR_EACH_PTR(tmp);

	add_ptr_list(&already_added, sym);
	add_ptr_list(&inlines_called, sym);
}

static void process_inlines(void)
{
	struct symbol *tmp;

	FOR_EACH_PTR(inlines_called, tmp) {
		split_function(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_ptr_list(&inlines_called);
}

static struct symbol *get_last_scoped_symbol(struct symbol_list *big_list, int use_static)
{
	struct symbol *sym;

	FOR_EACH_PTR_REVERSE(big_list, sym) {
		if (!sym->scope)
			continue;
		if (use_static && sym->ctype.modifiers & MOD_STATIC)
			return sym;
		if (!use_static && !(sym->ctype.modifiers & MOD_STATIC))
			return sym;
	} END_FOR_EACH_PTR_REVERSE(sym);

	return NULL;
}

static bool interesting_function(struct symbol *sym)
{
	static int prev_stream = -1;
	static bool prev_answer;
	const char *filename;
	int len;

	if (!(sym->ctype.modifiers & MOD_INLINE))
		return true;

	if (sym->pos.stream == prev_stream)
		return prev_answer;

	prev_stream = sym->pos.stream;
	prev_answer = false;

	filename = stream_name(sym->pos.stream);
	len = strlen(filename);
	if (len > 0 && filename[len - 1] == 'c')
		prev_answer = true;
	return prev_answer;
}

static void split_inlines_in_scope(struct symbol *sym)
{
	struct symbol *base;
	struct symbol_list *scope_list;
	int stream;

	scope_list = sym->scope->symbols;
	stream = sym->pos.stream;

	/* find the last static symbol in the file */
	FOR_EACH_PTR_REVERSE(scope_list, sym) {
		if (sym->pos.stream != stream)
			continue;
		if (sym->type != SYM_NODE)
			continue;
		base = get_base_type(sym);
		if (!base)
			continue;
		if (base->type != SYM_FN)
			continue;
		if (!base->inline_stmt)
			continue;
		if (!interesting_function(sym))
			continue;
		add_inline_function(sym);
	} END_FOR_EACH_PTR_REVERSE(sym);

	process_inlines();
}

static void split_inlines(struct symbol_list *sym_list)
{
	struct symbol *sym;

	sym = get_last_scoped_symbol(sym_list, 0);
	if (sym)
		split_inlines_in_scope(sym);
	sym = get_last_scoped_symbol(sym_list, 1);
	if (sym)
		split_inlines_in_scope(sym);
}

static struct stree *clone_estates_perm(struct stree *orig)
{
	struct stree *ret = NULL;
	struct sm_state *tmp;

	FOR_EACH_SM(orig, tmp) {
		set_state_stree_perm(&ret, tmp->owner, tmp->name, tmp->sym, clone_estate_perm(tmp->state));
	} END_FOR_EACH_SM(tmp);

	return ret;
}

struct position last_pos;
static void split_c_file_functions(struct symbol_list *sym_list)
{
	struct symbol *sym;

	__unnullify_path();
	FOR_EACH_PTR(sym_list, sym) {
		set_position(sym->pos);
		if (sym->type != SYM_NODE || get_base_type(sym)->type != SYM_FN) {
			__pass_to_client(sym, BASE_HOOK);
			fake_global_assign(sym);
			__pass_to_client(sym, DECLARATION_HOOK_AFTER);
		}
	} END_FOR_EACH_PTR(sym);
	global_states = clone_estates_perm(get_all_states_stree(SMATCH_EXTRA));
	nullify_path();

	FOR_EACH_PTR(sym_list, sym) {
		set_position(sym->pos);
		last_pos = sym->pos;
		if (!interesting_function(sym))
			continue;
		if (sym->type == SYM_NODE && get_base_type(sym)->type == SYM_FN) {
			split_function(sym);
			process_inlines();
		}
		last_pos = sym->pos;
	} END_FOR_EACH_PTR(sym);
	split_inlines(sym_list);
	__pass_to_client(sym_list, END_FILE_HOOK);
}

static int final_before_fake;
void init_fake_env(void)
{
	if (!in_fake_env)
		final_before_fake = final_pass;
	in_fake_env++;
	__push_fake_cur_stree();
	final_pass = 0;
}

void end_fake_env(void)
{
	__free_fake_cur_stree();
	in_fake_env--;
	if (!in_fake_env)
		final_pass = final_before_fake;
}

static void open_output_files(char *base_file)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "%s.smatch", base_file);
	sm_outfd = fopen(buf, "w");
	if (!sm_outfd)
		sm_fatal("Cannot open %s", buf);

	if (!option_info)
		return;

	snprintf(buf, sizeof(buf), "%s.smatch.sql", base_file);
	sql_outfd = fopen(buf, "w");
	if (!sql_outfd)
		sm_fatal("Error:  Cannot open %s", buf);

	snprintf(buf, sizeof(buf), "%s.smatch.caller_info", base_file);
	caller_info_fd = fopen(buf, "w");
	if (!caller_info_fd)
		sm_fatal("Error:  Cannot open %s", buf);
}

void smatch(struct string_list *filelist)
{
	struct symbol_list *sym_list;
	struct timeval stop, start;
	char *path;
	int len;

	gettimeofday(&start, NULL);

	FOR_EACH_PTR_NOTAG(filelist, base_file) {
		path = getcwd(NULL, 0);
		free(full_base_file);
		if (path) {
			len = strlen(path) + 1 + strlen(base_file) + 1;
			full_base_file = malloc(len);
			snprintf(full_base_file, len, "%s/%s", path, base_file);
		} else {
			full_base_file = alloc_string(base_file);
		}
		if (option_file_output)
			open_output_files(base_file);
		base_file_stream = input_stream_nr;
		sym_list = sparse_keep_tokens(base_file);
		split_c_file_functions(sym_list);
	} END_FOR_EACH_PTR_NOTAG(base_file);

	gettimeofday(&stop, NULL);

	set_position(last_pos);
	final_pass = 1;
	if (option_time)
		sm_msg("time: %lu", stop.tv_sec - start.tv_sec);
	if (option_mem)
		sm_msg("mem: %luKb", get_max_memory());
}
