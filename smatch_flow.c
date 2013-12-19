/*
 * sparse/smatch_flow.c
 *
 * Copyright (C) 2006,2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
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

int final_pass;
int __inline_call;
struct expression  *__inline_fn;

static int __smatch_lineno = 0;

static char *base_file;
static const char *filename;
static char *pathname;
static char *full_filename;
static char *cur_func;
static int loop_count;
int __expr_stmt_count;
int __in_function_def;
static struct expression_list *switch_expr_stack = NULL;
static struct expression_list *post_op_stack = NULL;

struct expression_list *big_expression_stack;
struct statement_list *big_statement_stack;
int __in_pre_condition = 0;
int __bail_on_rest_of_function = 0;
char *get_function(void) { return cur_func; }
int get_lineno(void) { return __smatch_lineno; }
int inside_loop(void) { return !!loop_count; }
struct expression *get_switch_expr(void) { return top_expression(switch_expr_stack); }
int in_expression_statement(void) { return !!__expr_stmt_count; }

static void split_symlist(struct symbol_list *sym_list);
static void split_declaration(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list);
static void add_inline_function(struct symbol *sym);
static void parse_inline(struct expression *expr);

int option_assume_loops = 0;
int option_known_conditions = 0;
int option_two_passes = 0;
struct symbol *cur_func_sym = NULL;

int outside_of_function(void)
{
	return cur_func_sym == NULL;
}

const char *get_filename(void)
{
	if (option_info)
		return base_file;
	if (option_full_path)
		return full_filename;
	return filename;
}

const char *get_base_file(void)
{
	return base_file;
}

static void set_position(struct position pos)
{
	int len;
	static int prev_stream = -1;

	if (pos.stream == 0 && pos.line == 0)
		return;

	__smatch_lineno = pos.line;

	if (pos.stream == prev_stream)
		return;

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

static int is_inline_func(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;
	if (expr->symbol->ctype.modifiers & MOD_INLINE)
		return 1;
	return 0;
}

static int is_noreturn_func(struct expression *expr)
{
	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;
	if (expr->symbol->ctype.modifiers & MOD_NORETURN)
		return 1;
	return 0;
}

int inlinable(struct expression *expr)
{
	struct symbol *sym;

	if (__inline_fn)  /* don't nest */
		return 0;

	if (expr->type != EXPR_SYMBOL || !expr->symbol)
		return 0;
	if (is_no_inline_function(expr->symbol->ident->name))
		return 0;
	sym = get_base_type(expr->symbol);
	if (sym->stmt && sym->stmt->type == STMT_COMPOUND) {
		if (ptr_list_size((struct ptr_list *)sym->stmt->stmts) <= 10)
			return 1;
		return 0;
	}
	if (sym->inline_stmt && sym->inline_stmt->type == STMT_COMPOUND) {
		if (ptr_list_size((struct ptr_list *)sym->inline_stmt->stmts) <= 10)
			return 1;
		return 0;
	}
	return 0;
}

void __process_post_op_stack(void)
{
	struct expression *expr;

	FOR_EACH_PTR(post_op_stack, expr) {
		__pass_to_client(expr, OP_HOOK);
	} END_FOR_EACH_PTR(expr);

	__free_ptr_list((struct ptr_list **)&post_op_stack);
}

void __split_expr(struct expression *expr)
{
	if (!expr)
		return;

	// sm_msg(" Debug expr_type %d %s", expr->type, show_special(expr->op));

	push_expression(&big_expression_stack, expr);
	set_position(expr->pos);
	__pass_to_client(expr, EXPR_HOOK);

	switch (expr->type) {
	case EXPR_PREOP:
		if (expr->op == '*')
			__pass_to_client(expr, DEREF_HOOK);
		__split_expr(expr->unop);
		__pass_to_client(expr, OP_HOOK);
		break;
	case EXPR_POSTOP:
		__split_expr(expr->unop);
		push_expression(&post_op_stack, expr);
		break;
	case EXPR_STATEMENT:
		__expr_stmt_count++;
		__split_stmt(expr->statement);
		__expr_stmt_count--;
		break;
	case EXPR_LOGICAL:
	case EXPR_COMPARE:
		__pass_to_client(expr, LOGIC_HOOK);
		__handle_logic(expr);
		break;
	case EXPR_BINOP:
		__pass_to_client(expr, BINOP_HOOK);
	case EXPR_COMMA:
		__split_expr(expr->left);
		__process_post_op_stack();
		__split_expr(expr->right);
		break;
	case EXPR_ASSIGNMENT: {
		struct expression *tmp;

		if (!expr->right)
			break;

		__pass_to_client(expr, RAW_ASSIGNMENT_HOOK);

		/* foo = !bar() */
		if (__handle_condition_assigns(expr))
			break;
		/* foo = (x < 5 ? foo : 5); */
		if (__handle_select_assigns(expr))
			break;
		/* foo = ({frob(); frob(); frob(); 1;}) */
		if (__handle_expr_statement_assigns(expr))
			break;

		__split_expr(expr->right);
		if (outside_of_function())
			__pass_to_client(expr, GLOBAL_ASSIGNMENT_HOOK);
		else
			__pass_to_client(expr, ASSIGNMENT_HOOK);
		tmp = strip_expr(expr->right);
		if (tmp->type == EXPR_CALL)
			__pass_to_client(expr, CALL_ASSIGNMENT_HOOK);
		if (get_macro_name(tmp->pos) &&
		    get_macro_name(expr->pos) != get_macro_name(tmp->pos))
			__pass_to_client(expr, MACRO_ASSIGNMENT_HOOK);
		__split_expr(expr->left);
		break;
	}
	case EXPR_DEREF:
		__pass_to_client(expr, DEREF_HOOK);
		__split_expr(expr->deref);
		break;
	case EXPR_SLICE:
		__split_expr(expr->base);
		break;
	case EXPR_CAST:
	case EXPR_FORCE_CAST:
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
		evaluate_expression(expr);
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
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
		if (sym_name_is("__builtin_constant_p", expr->fn))
			break;
		split_expr_list(expr->args);
		__split_expr(expr->fn);
		if (is_inline_func(expr->fn))
			add_inline_function(expr->fn->symbol);
		if (inlinable(expr->fn))
			__inline_call = 1;
		__process_post_op_stack();
		__pass_to_client(expr, FUNCTION_CALL_HOOK);
		__inline_call = 0;
		if (inlinable(expr->fn)) {
			parse_inline(expr);
		}
		__pass_to_client(expr, CALL_HOOK_AFTER_INLINE);
		if (is_noreturn_func(expr->fn))
			nullify_path();
		break;
	case EXPR_INITIALIZER:
		split_expr_list(expr->expr_list);
		break;
	case EXPR_IDENTIFIER:
		__split_expr(expr->ident_expression);
		break;
	case EXPR_INDEX:
		__split_expr(expr->idx_expression);
		break;
	case EXPR_POS:
		__split_expr(expr->init_expr);
		break;
	case EXPR_SYMBOL:
		__pass_to_client(expr, SYM_HOOK);
		break;
	case EXPR_STRING:
		__pass_to_client(expr, STRING_HOOK);
		break;
	default:
		break;
	};
	pop_expression(&big_expression_stack);
}

static int is_forever_loop(struct statement *stmt)
{
	struct expression *expr;

	expr = strip_expr(stmt->iterator_pre_condition);
	if (!expr)
		expr = stmt->iterator_post_condition;
	if (!expr) {
		/* this is a for(;;) loop... */
		return 1;
	}

	if (expr->type == EXPR_VALUE && expr->value == 1)
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

/*
 * Pre Loops are while and for loops.
 */
static void handle_pre_loop(struct statement *stmt)
{
	int once_through; /* we go through the loop at least once */
	struct sm_state *extra_sm = NULL;
	int unchanged = 0;
	char *loop_name;
	struct state_list *slist = NULL;
	struct sm_state *sm = NULL;

	loop_name = get_loop_name(loop_num);
	loop_num++;

	__split_stmt(stmt->iterator_pre_statement);

	once_through = implied_condition_true(stmt->iterator_pre_condition);

	loop_count++;
	__push_continues();
	__push_breaks();

	__merge_gotos(loop_name);

	extra_sm = __extra_handle_canonical_loops(stmt, &slist);
	__in_pre_condition++;
	__pass_to_client(stmt, PRELOOP_HOOK);
	__split_whole_condition(stmt->iterator_pre_condition);
	__in_pre_condition--;
	FOR_EACH_PTR(slist, sm) {
		set_state(sm->owner, sm->name, sm->sym, sm->state);
	} END_FOR_EACH_PTR(sm);
	free_slist(&slist);
	if (extra_sm)
		extra_sm = get_sm_state(extra_sm->owner, extra_sm->name, extra_sm->sym);

	if (option_assume_loops)
		once_through = 1;

	__split_stmt(stmt->iterator_statement);
	__warn_on_silly_pre_loops();
	if (is_forever_loop(stmt)) {
		struct state_list *slist;

		__save_gotos(loop_name);

		__push_fake_cur_slist();
		__split_stmt(stmt->iterator_post_statement);
		slist = __pop_fake_cur_slist();

		__discard_continues();
		__discard_false_states();
		__use_breaks();

		if (!__path_is_null())
			__merge_slist_into_cur(slist);
		free_slist(&slist);
	} else {
		__merge_continues();
		unchanged = __iterator_unchanged(extra_sm);
		__split_stmt(stmt->iterator_post_statement);
		__save_gotos(loop_name);
		__split_whole_condition(stmt->iterator_pre_condition);
		nullify_path();
		__merge_false_states();
		if (once_through)
			__discard_false_states();
		else
			__merge_false_states();

		if (extra_sm && unchanged)
			__extra_pre_loop_hook_after(extra_sm,
						stmt->iterator_post_statement,
						stmt->iterator_pre_condition);
		__merge_breaks();
	}
	loop_count--;
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

	__push_continues();
	__push_breaks();
	__merge_gotos(loop_name);
	__split_stmt(stmt->iterator_statement);
	__merge_continues();
	if (!is_zero(stmt->iterator_post_condition))
		__save_gotos(loop_name);

	if (is_forever_loop(stmt)) {
		__use_breaks();
	} else {
		__split_whole_condition(stmt->iterator_post_condition);
		__use_false_states();
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

static int last_stmt_on_same_line()
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

static struct statement *last_stmt;
static int is_last_stmt(struct statement *stmt)
{
	if (stmt == last_stmt)
		return 1;
	return 0;
}

static void print_unreached_initializers(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		if (sym->initializer)
			sm_msg("info: '%s' is not actually initialized (unreached code).",
				(sym->ident ? sym->ident->name : "this variable"));
	} END_FOR_EACH_PTR(sym);
}

static void print_unreached(struct statement *stmt)
{
	static int print = 1;

	if (__inline_fn)
		return;

	if (!__path_is_null()) {
		print = 1;
		return;
	}
	if (!print)
		return;

	switch (stmt->type) {
	case STMT_COMPOUND: /* after a switch before a case stmt */
	case STMT_RANGE:
	case STMT_CASE:
	case STMT_LABEL:
		return;
	case STMT_DECLARATION: /* switch (x) { int a; case foo: ... */
		print_unreached_initializers(stmt->declaration);
		return;
	case STMT_RETURN: /* gcc complains if you don't have a return statement */
		if (is_last_stmt(stmt))
			return;
		break;
	case STMT_GOTO:
		if (!option_spammy)
			return;
		break;
	default:
		break;
	}
	if (!option_spammy && empty_statement(stmt))
		return;
	sm_msg("info: ignoring unreachable code.");
	print = 0;
}

static void split_asm_constraints(struct expression_list *expr_list)
{
	struct expression *expr;
	int state = 0;

	FOR_EACH_PTR(expr_list, expr) {
		switch (state) {
		case 0: /* identifier */
		case 1: /* constraint */
			state++;
			continue;
		case 2: /* expression */
			state = 0;
			__split_expr(expr);
			continue;
		}
	} END_FOR_EACH_PTR(expr);
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

static void split_known_switch(struct statement *stmt, sval_t sval)
{
	struct statement *tmp;

	__split_expr(stmt->switch_expression);

	push_expression(&switch_expr_stack, stmt->switch_expression);
	__save_switch_states(top_expression(switch_expr_stack));
	nullify_path();
	__push_default();
	__push_breaks();

	stmt = stmt->switch_statement;

	if (!last_stmt)
		last_stmt = last_ptr_list((struct ptr_list *)stmt->stmts);

	__push_scope_hooks();
	FOR_EACH_PTR(stmt->stmts, tmp) {
		__smatch_lineno = tmp->pos.line;
		if (is_case_val(tmp, sval)) {
			__merge_switches(top_expression(switch_expr_stack),
					 stmt->case_expression);
			__pass_case_to_client(top_expression(switch_expr_stack),
					      stmt->case_expression);
		}
		if (__path_is_null())
			continue;
		__split_stmt(tmp);
		if (__path_is_null()) {
			__set_default();
			goto out;
		}
	} END_FOR_EACH_PTR(tmp);
out:
	__call_scope_hooks();
	if (!__pop_default())
		__merge_switches(top_expression(switch_expr_stack),
				 NULL);
	__discard_switches();
	__merge_breaks();
	pop_expression(&switch_expr_stack);
}

void __split_stmt(struct statement *stmt)
{
	sval_t sval;

	if (!stmt)
		goto out;

	if (out_of_memory() || __bail_on_rest_of_function) {
		static char *printed = NULL;

		if (printed != cur_func)
			sm_msg("Function too hairy.  Giving up.");
		final_pass = 0;  /* turn off sm_msg() from here */
		printed = cur_func;
		return;
	}

	add_ptr_list(&big_statement_stack, stmt);
	free_expression_stack(&big_expression_stack);
	set_position(stmt->pos);
	print_unreached(stmt);
	__pass_to_client(stmt, STMT_HOOK);

	switch (stmt->type) {
	case STMT_DECLARATION:
		split_declaration(stmt->declaration);
		break;
	case STMT_RETURN:
		__split_expr(stmt->ret_value);
		__pass_to_client(stmt->ret_value, RETURN_HOOK);
		nullify_path();
		break;
	case STMT_EXPRESSION:
		__split_expr(stmt->expression);
		break;
	case STMT_COMPOUND: {
		struct statement *tmp;

		if (!last_stmt)
			last_stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
		__push_scope_hooks();
		FOR_EACH_PTR(stmt->stmts, tmp) {
			__split_stmt(tmp);
		} END_FOR_EACH_PTR(tmp);
		__call_scope_hooks();
		break;
	}
	case STMT_IF:
		if (known_condition_true(stmt->if_conditional)) {
			__split_stmt(stmt->if_true);
			break;
		}
		if (known_condition_false(stmt->if_conditional)) {
			__split_stmt(stmt->if_false);
			break;
		}
		if (option_known_conditions &&
		    implied_condition_true(stmt->if_conditional)) {
			sm_info("this condition is true.");
			__split_stmt(stmt->if_true);
			break;
		}
		if (option_known_conditions &&
		    implied_condition_false(stmt->if_conditional)) {
			sm_info("this condition is false.");
			__split_stmt(stmt->if_false);
			break;
		}
		__split_whole_condition(stmt->if_conditional);
		__split_stmt(stmt->if_true);
		if (empty_statement(stmt->if_true) &&
			last_stmt_on_same_line() &&
			!get_macro_name(stmt->if_true->pos))
			sm_msg("warn: if();");
		__push_true_states();
		__use_false_states();
		__split_stmt(stmt->if_false);
		__merge_true_states();
		break;
	case STMT_ITERATOR:
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
		if (!__pop_default())
			__merge_switches(top_expression(switch_expr_stack),
				      NULL);
		__discard_switches();
		__merge_breaks();
		pop_expression(&switch_expr_stack);
		break;
	case STMT_CASE:
		__merge_switches(top_expression(switch_expr_stack),
				      stmt->case_expression);
		__pass_case_to_client(top_expression(switch_expr_stack),
				      stmt->case_expression);
		if (!stmt->case_expression)
			__set_default();
		__split_expr(stmt->case_expression);
		__split_expr(stmt->case_to);
		__split_stmt(stmt->case_statement);
		break;
	case STMT_LABEL:
		if (stmt->label_identifier &&
		    stmt->label_identifier->type == SYM_LABEL &&
		    stmt->label_identifier->ident) {
			loop_count = 1000000;
			__merge_gotos(stmt->label_identifier->ident->name);
		}
		__split_stmt(stmt->label_statement);
		break;
	case STMT_GOTO:
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
			__save_gotos(stmt->goto_label->ident->name);
		}
		nullify_path();
		break;
	case STMT_NONE:
		break;
	case STMT_ASM:
		__pass_to_client(stmt, ASM_HOOK);
		__split_expr(stmt->asm_string);
		split_asm_constraints(stmt->asm_outputs);
		split_asm_constraints(stmt->asm_inputs);
		split_asm_constraints(stmt->asm_clobbers);
		break;
	case STMT_CONTEXT:
		break;
	case STMT_RANGE:
		__split_expr(stmt->range_expression);
		__split_expr(stmt->range_low);
		__split_expr(stmt->range_high);
		break;
	}
out:
	__process_post_op_stack();
}

static void split_expr_list(struct expression_list *expr_list)
{
	struct expression *expr;

	FOR_EACH_PTR(expr_list, expr) {
		__split_expr(expr);
		__process_post_op_stack();
	} END_FOR_EACH_PTR(expr);
}

static void split_sym(struct symbol *sym)
{
	if (!sym)
		return;
	if (!(sym->namespace & NS_SYMBOL))
		return;

	__split_stmt(sym->stmt);
	__split_expr(sym->array_size);
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

struct member_set {
	struct ident *ident;
	int set;
};

static struct member_set *alloc_member_set(struct symbol *type)
{
	struct member_set *member_set;
	struct symbol *member;
	int member_count;
	int member_idx;

	member_count = ptr_list_size((struct ptr_list *)type->symbol_list);
	member_set = malloc(member_count * sizeof(*member_set));
	member_idx = 0;
	FOR_EACH_PTR(type->symbol_list, member) {
		member_set[member_idx].ident = member->ident;
		member_set[member_idx].set = 0;
		member_idx++;
	} END_FOR_EACH_PTR(member);

	return member_set;
}

static void mark_member_as_set(struct symbol *type, struct member_set *member_set, struct ident *ident)
{
	int member_count = ptr_list_size((struct ptr_list *)type->symbol_list);
	int i;

	for (i = 0; i < member_count; i++) {
		if (member_set[i].ident == ident) {
			member_set[i].set = 1;
			return;
		}
	}
//	crap.  this is buggy.
//	sm_msg("internal smatch error in initializer %s.%s", type->ident->name, ident->name);
}

static void set_unset_to_zero(struct expression *symbol, struct symbol *type, struct member_set *member_set)
{
	struct expression *deref, *assign;
	struct symbol *member, *member_type;
	int member_idx;

	member_idx = 0;
	FOR_EACH_PTR(type->symbol_list, member) {
		if (!member->ident || member_set[member_idx].set) {
			member_idx++;
			continue;
		}
		member_type = get_real_base_type(member);
		if (!member_type || member_type->type == SYM_ARRAY)
			continue;
		/* TODO: this should be handled recursively and not ignored */
		if (member_type->type == SYM_STRUCT || member_type->type == SYM_UNION)
			continue;
		deref = member_expression(symbol, '.', member->ident);
		assign = assign_expression(deref, zero_expr());
		__split_expr(assign);
		member_idx++;
	} END_FOR_EACH_PTR(member);

}

static void fake_member_assigns_helper(struct expression *symbol, struct expression_list *members, fake_cb *fake_cb)
{
	struct expression *deref, *assign, *tmp;
	struct symbol *struct_type, *type;
	struct ident *member;
	int member_idx;
	struct member_set *member_set;

	struct_type = get_type(symbol);
	if (!struct_type ||
	    (struct_type->type != SYM_STRUCT && struct_type->type != SYM_UNION))
		return;

	member_set = alloc_member_set(struct_type);

	member_idx = 0;
	FOR_EACH_PTR(members, tmp) {
		member = number_to_member(symbol, member_idx);
		while (tmp->type == EXPR_IDENTIFIER) {
			member = tmp->expr_ident;
			member_idx = member_to_number(symbol, member);
			tmp = tmp->ident_expression;
		}
		mark_member_as_set(struct_type, member_set, member);
		member_idx++;
		deref = member_expression(symbol, '.', member);
		if (tmp->type == EXPR_INITIALIZER) {
			type = get_type(deref);
			if (type && type->type == SYM_ARRAY)
				fake_element_assigns_helper(deref, tmp->expr_list, fake_cb);
			else
				fake_member_assigns_helper(deref, tmp->expr_list, fake_cb);
		} else {
			assign = assign_expression(deref, tmp);
			fake_cb(assign);
		}
	} END_FOR_EACH_PTR(tmp);

	set_unset_to_zero(symbol, struct_type, member_set);
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
	int idx;

	idx = 0;
	FOR_EACH_PTR(expr_list, tmp) {
		if (tmp->type == EXPR_INDEX) {
			if (tmp->idx_from != tmp->idx_to)
				return;
			idx = tmp->idx_from;
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
			assign = assign_expression(binop, tmp);
			fake_cb(assign);
		}
next:
		idx++;
	} END_FOR_EACH_PTR(tmp);
}

static void fake_element_assigns(struct symbol *sym, fake_cb *fake_cb)
{
	fake_element_assigns_helper(symbol_expression(sym), sym->initializer->expr_list, fake_cb);
}

static void fake_assign_expr(struct symbol *sym)
{
	struct expression *assign, *symbol;

	symbol = symbol_expression(sym);
	assign = assign_expression(symbol, sym->initializer);
	__split_expr(assign);
}

static void call_split_expr(struct expression *expr)
{
	__split_expr(expr);
}

static void do_initializer_stuff(struct symbol *sym)
{
	if (!sym->initializer)
		return;

	if (sym->initializer->type == EXPR_INITIALIZER) {
		if (get_real_base_type(sym)->type == SYM_ARRAY)
			fake_element_assigns(sym, call_split_expr);
		else
			fake_member_assigns(sym, call_split_expr);
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

	if (!sym->initializer)
		return;
	if (sym->initializer->type == EXPR_INITIALIZER) {
		if (get_real_base_type(sym)->type == SYM_ARRAY)
			fake_element_assigns(sym, call_global_assign_hooks);
		else
			fake_member_assigns(sym, call_global_assign_hooks);
	} else {
		symbol = symbol_expression(sym);
		assign = assign_expression(symbol, sym->initializer);
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

static void split_function(struct symbol *sym)
{
	struct symbol *base_type = get_base_type(sym);

	cur_func_sym = sym;
	if (sym->ident)
		cur_func = sym->ident->name;
	__smatch_lineno = sym->pos.line;
	last_stmt = NULL;
	loop_count = 0;
	sm_debug("new function:  %s\n", cur_func);
	__slist_id = 0;
	if (option_two_passes) {
		__unnullify_path();
		loop_num = 0;
		final_pass = 0;
		start_function_definition(sym);
		__split_stmt(base_type->stmt);
		__split_stmt(base_type->inline_stmt);
		nullify_path();
	}
	__unnullify_path();
	loop_num = 0;
	start_function_definition(sym);
	__split_stmt(base_type->stmt);
	__split_stmt(base_type->inline_stmt);
	__pass_to_client(sym, END_FUNC_HOOK);
	__pass_to_client(sym, AFTER_FUNC_HOOK);
	cur_func_sym = NULL;
	cur_func = NULL;
	clear_all_states();
	free_data_info_allocs();
	free_expression_stack(&switch_expr_stack);
	__free_ptr_list((struct ptr_list **)&big_statement_stack);
	__bail_on_rest_of_function = 0;
}

static void parse_inline(struct expression *call)
{
	struct symbol *base_type;
	int loop_num_bak = loop_num;
	int final_pass_bak = final_pass;
	char *cur_func_bak = cur_func;
	struct statement_list *big_statement_stack_bak = big_statement_stack;
	struct expression_list *big_expression_stack_bak = big_expression_stack;
	struct expression_list *switch_expr_stack_bak = switch_expr_stack;
	struct symbol *cur_func_sym_bak = cur_func_sym;

	__pass_to_client(call, INLINE_FN_START);
	final_pass = 0;  /* don't print anything */
	__inline_fn = call;

	base_type = get_base_type(call->fn->symbol);
	cur_func_sym = call->fn->symbol;
	if (call->fn->symbol->ident)
		cur_func = call->fn->symbol->ident->name;
	else
		cur_func = NULL;
	set_position(call->fn->symbol->pos);

	save_all_states();
	nullify_all_states();
	big_statement_stack = NULL;
	big_expression_stack = NULL;
	switch_expr_stack = NULL;

	sm_debug("inline function:  %s\n", cur_func);
	__unnullify_path();
	loop_num = 0;
	start_function_definition(call->fn->symbol);
	__split_stmt(base_type->stmt);
	__split_stmt(base_type->inline_stmt);
	__pass_to_client(call->fn->symbol, END_FUNC_HOOK);
	__pass_to_client(call->fn->symbol, AFTER_FUNC_HOOK);

	free_expression_stack(&switch_expr_stack);
	__free_ptr_list((struct ptr_list **)&big_statement_stack);
	nullify_path();

	loop_num = loop_num_bak;
	final_pass = final_pass_bak;
	cur_func_sym = cur_func_sym_bak;
	cur_func = cur_func_bak;
	big_statement_stack = big_statement_stack_bak;
	big_expression_stack = big_expression_stack_bak;
	switch_expr_stack = switch_expr_stack_bak;

	restore_all_states();
	set_position(call->pos);
	__inline_fn = NULL;
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

static void process_inlines()
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

static void split_functions(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		set_position(sym->pos);
		if (sym->type == SYM_NODE && get_base_type(sym)->type == SYM_FN) {
			split_function(sym);
			process_inlines();
		} else {
			__pass_to_client(sym, BASE_HOOK);
			fake_global_assign(sym);
		}
	} END_FOR_EACH_PTR(sym);
	split_inlines(sym_list);
	__pass_to_client(sym_list, END_FILE_HOOK);
}

void smatch(int argc, char **argv)
{

	struct string_list *filelist = NULL;
	struct symbol_list *sym_list;

	if (argc < 2) {
		printf("Usage:  smatch [--debug] <filename.c>\n");
		exit(1);
	}
	sparse_initialize(argc, argv, &filelist);
	FOR_EACH_PTR_NOTAG(filelist, base_file) {
		if (option_file_output) {
			char buf[256];

			snprintf(buf, sizeof(buf), "%s.smatch", base_file);
			sm_outfd = fopen(buf, "w");
			if (!sm_outfd) {
				printf("Error:  Cannot open %s\n", base_file);
				exit(1);
			}
		}
		sym_list = sparse_keep_tokens(base_file);
		split_functions(sym_list);
	} END_FOR_EACH_PTR_NOTAG(base_file);
}
