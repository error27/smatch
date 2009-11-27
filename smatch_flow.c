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
#include "smatch.h"
#include "smatch_expression_stacks.h"
#include "smatch_extra.h"
#include "smatch_slist.h" // just for sname.

int final_pass;

static int __smatch_lineno = 0;

static char *filename;
static char *pathname;
static char *full_filename;
static char *cur_func;
static int line_func_start;
static struct expression_stack *switch_expr_stack = NULL;

char *get_function(void) { return cur_func; }
int get_lineno(void) { return __smatch_lineno; }
int get_func_pos(void) { return __smatch_lineno - line_func_start; }

static void split_symlist(struct symbol_list *sym_list);
static void split_declaration(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list);

int option_assume_loops = 0;
int option_known_conditions = 0;
int option_two_passes = 0;
struct symbol *cur_func_sym = NULL;

char *get_filename(void)
{
	if (option_full_path)
		return full_filename;
	return filename;
}

void __split_expr(struct expression *expr)
{
	if (!expr)
		return;

	// printf("%d Debug expr_type %d %s\n", get_lineno(), expr->type, show_special(expr->op));

	__smatch_lineno = expr->pos.line;
	__pass_to_client(expr, EXPR_HOOK);

	switch (expr->type) {
	case EXPR_PREOP: 
		if (expr->op == '*')
			__pass_to_client(expr, DEREF_HOOK);
	case EXPR_POSTOP:
		__pass_to_client(expr, OP_HOOK);
		__split_expr(expr->unop);
		return;
	case EXPR_STATEMENT:
		__split_statements(expr->statement);
		return;
	case EXPR_LOGICAL:
		__split_whole_condition(expr);
		__push_true_states();
		__use_false_states();
		__merge_true_states();
		__pop_false_only_stack();
		return;

		return;
	case EXPR_BINOP: 
	case EXPR_COMMA:
	case EXPR_COMPARE:
		__split_expr(expr->left);
		__split_expr(expr->right);		
		return;
	case EXPR_ASSIGNMENT: {
		struct expression *tmp;

		__split_expr(expr->right);
		__pass_to_client(expr, ASSIGNMENT_HOOK);
		tmp = strip_expr(expr->right);
		if (tmp->type == EXPR_CALL)
			__pass_to_client(expr, CALL_ASSIGNMENT_HOOK);
		__split_expr(expr->left);
		return;
	}
	case EXPR_DEREF:
		__pass_to_client(expr, DEREF_HOOK);
		__split_expr(expr->deref);
		return;
	case EXPR_SLICE:
		__split_expr(expr->base);
		return;
	case EXPR_CAST:
		__split_expr(expr->cast_expression);
		return;
	case EXPR_SIZEOF:
		/* there isn't anything to pass a client from inside a sizeof() */
		return;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		__split_whole_condition(expr->conditional);
		__split_expr(expr->cond_true);
		__push_true_states();
		__use_false_states();
		__split_expr(expr->cond_false);
		__merge_true_states();
		__pop_false_only_stack();
		return;
	case EXPR_CALL:
		split_expr_list(expr->args);
		__split_expr(expr->fn);
		__pass_to_client(expr, FUNCTION_CALL_HOOK);
		return;
	case EXPR_INITIALIZER:
		split_expr_list(expr->expr_list);
		return;
	case EXPR_IDENTIFIER:
		__split_expr(expr->ident_expression);
		return;
	case EXPR_INDEX:
		__split_expr(expr->idx_expression);
		return;
	case EXPR_POS:
		__split_expr(expr->init_expr);
		return;
	default:
		return;
	};
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

	if (expr->type == EXPR_VALUE && expr->value == 1) {
		return 1;
	}

	return 0;
}

static int loop_num;
static char *get_loop_name(int num)
{
	char buf[256];

	snprintf(buf, 255, "-loop%d", num);
	buf[255] = '\0';
	return alloc_sname(buf);;
}

/*
 * Pre Loops are while and for loops.
 */

static void handle_pre_loop(struct statement *stmt)
{
	int once_through; /* we go through the loop at least once */
	struct sm_state *extra_state = NULL;
	int unchanged = 0;
	char *loop_name;

 	loop_name = get_loop_name(loop_num);
	loop_num++;

	__split_statements(stmt->iterator_pre_statement);

	once_through = implied_condition_true(stmt->iterator_pre_condition);

	__push_continues();
	__push_breaks();

	__merge_gotos(loop_name);
	__split_whole_condition(stmt->iterator_pre_condition);
	if (once_through)
		extra_state = __extra_pre_loop_hook_before(stmt->iterator_pre_statement);
	if (option_assume_loops)
		once_through = 1;
	__split_statements(stmt->iterator_statement);

	__warn_on_silly_pre_loops();	
	if (is_forever_loop(stmt)) {
		__save_gotos(loop_name);
		__pop_false_only_stack();
		/* forever loops don't have an iterator_post_statement */
		__pop_continues();
		__pop_false_states();
		__use_breaks();
	} else if (once_through) {
		__merge_continues();
		if (extra_state)
			unchanged = __iterator_unchanged(extra_state, stmt->iterator_post_statement);
		__split_statements(stmt->iterator_post_statement);
		__save_gotos(loop_name);
		__split_whole_condition(stmt->iterator_pre_condition);
		nullify_path();
		__merge_false_states();
		if (extra_state && unchanged)
			__extra_pre_loop_hook_after(extra_state,
				stmt->iterator_post_statement, stmt->iterator_pre_condition);
		__pop_false_states();
		__pop_false_only_stack();
		__pop_false_only_stack();
		__merge_breaks();
	} else {
		__merge_continues();
		__split_statements(stmt->iterator_post_statement);
		__save_gotos(loop_name);
		__merge_false_states();
		__use_false_only_stack();
		__merge_breaks();
	}
}

/*
 * Post loops are do {} while();
 */
static void handle_post_loop(struct statement *stmt)
{
	char *loop_name;

 	loop_name = get_loop_name(loop_num);
	loop_num++;

	__push_continues();
	__push_breaks();
	__merge_gotos(loop_name);
	__split_statements(stmt->iterator_statement);
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
	__pop_false_only_stack();
}

static void print_unreached(struct statement *stmt)
{

	/* 
	 * GCC insists on a return statement even where it is never
         * reached.  Also BUG() sometimes is a forever loop and
	 * sometimes not so people put code after a BUG().  There 
	 * are way to many false positives.
	 */
	return;

	if (__path_is_null()) {
		switch(stmt->type) {
		case STMT_COMPOUND: /* after a switch before a case stmt */
		case STMT_CASE:
		case STMT_LABEL:
		case STMT_DECLARATION: /* switch(x) { int a; case foo: ... */
			break;
		default:
			sm_msg("unreachable code. %d", stmt->type);
		}
	}
}

void __split_statements(struct statement *stmt)
{
	if (!stmt)
		return;
	
	if (out_of_memory()) {
		static char *printed = NULL;

		if (printed != cur_func)
			sm_msg("Function too big.  Giving up.");
		printed = cur_func;
		return;
	}

	__smatch_lineno = stmt->pos.line;
	print_unreached(stmt);
	__pass_to_client(stmt, STMT_HOOK);

	switch (stmt->type) {
	case STMT_DECLARATION:
		split_declaration(stmt->declaration);
		return;
	case STMT_RETURN:
		__split_expr(stmt->ret_value);
		__pass_to_client(stmt->ret_value, RETURN_HOOK);
		nullify_path();
		return;
	case STMT_EXPRESSION:
		__split_expr(stmt->expression);
		return;
	case STMT_COMPOUND: {
		struct statement *s;
		__push_scope_hooks();
		FOR_EACH_PTR(stmt->stmts, s) {
			__split_statements(s);
		} END_FOR_EACH_PTR(s);
		__call_scope_hooks();
		return;
	}
	case STMT_IF:
		if (known_condition_true(stmt->if_conditional)) {
			__split_statements(stmt->if_true);
			return;
		}
		if (known_condition_false(stmt->if_conditional)) {
			__split_statements(stmt->if_false);
			return;
		}
		if (option_known_conditions && 
		    implied_condition_true(stmt->if_conditional)) {
			sm_msg("info: this condition is true.");
			__split_statements(stmt->if_true);
			return;
		}
		if (option_known_conditions &&
		    implied_condition_false(stmt->if_conditional)) {
			sm_msg("info: this condition is false.");
			__split_statements(stmt->if_false);
			return;
		}
		__split_whole_condition(stmt->if_conditional);
		__split_statements(stmt->if_true);
		__push_true_states();
		__use_false_states();
		__split_statements(stmt->if_false);
		__merge_true_states();
		__pop_false_only_stack();
		return;
	case STMT_ITERATOR:
		if (stmt->iterator_pre_condition)
			handle_pre_loop(stmt);
		else if (stmt->iterator_post_condition)
			handle_post_loop(stmt);
		else {
			// these are for(;;) type loops.
			handle_pre_loop(stmt);
		}
		return;
	case STMT_SWITCH:
		__split_expr(stmt->switch_expression);
		push_expression(&switch_expr_stack, stmt->switch_expression);
		__save_switch_states(top_expression(switch_expr_stack));
		nullify_path();
		__push_default();
		__push_breaks();
		__split_statements(stmt->switch_statement);
		if (!__pop_default())
			__merge_switches(top_expression(switch_expr_stack),
				      NULL);
		__pop_switches();
		__merge_breaks();
		pop_expression(&switch_expr_stack);
		return;
	case STMT_CASE:
		__merge_switches(top_expression(switch_expr_stack),
				      stmt->case_expression);
		__pass_case_to_client(top_expression(switch_expr_stack),
				      stmt->case_expression);
		if (!stmt->case_expression)
			__set_default();
		__split_expr(stmt->case_expression);
		__split_expr(stmt->case_to);
		__split_statements(stmt->case_statement);
		return;
	case STMT_LABEL:
		if (stmt->label && 
		    stmt->label->type == SYM_LABEL && 
		    stmt->label->ident) {
			__merge_gotos(stmt->label->ident->name);
		}
		__split_statements(stmt->label_statement);
		return;
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
		return;
	case STMT_NONE:
		return;
	case STMT_ASM:
		__split_expr(stmt->asm_string);
		//__split_expr(stmt->asm_outputs);
		//__split_expr(stmt->asm_inputs);
		//__split_expr(stmt->asm_clobbers);
		return;
	case STMT_CONTEXT:
		return;
	case STMT_RANGE:
		__split_expr(stmt->range_expression);
		__split_expr(stmt->range_low);
		__split_expr(stmt->range_high);
		return;
	}
}

static void split_expr_list(struct expression_list *expr_list)
{
	struct expression *expr;
	FOR_EACH_PTR(expr_list, expr) {
		__split_expr(expr);
	} END_FOR_EACH_PTR(expr);
}


static void split_sym(struct symbol *sym)
{
	if (!sym)
		return;
	if (!(sym->namespace & NS_SYMBOL))
		return;

	__pass_to_client(sym, SYM_HOOK);
	__split_statements(sym->stmt);
	__split_expr(sym->array_size);
	split_symlist(sym->arguments);
	split_symlist(sym->symbol_list);
	__split_statements(sym->inline_stmt);
	split_symlist(sym->inline_symbol_list);
	__split_expr(sym->initializer);
}

static void split_symlist(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		split_sym(sym);
	} END_FOR_EACH_PTR(sym);
}

static struct expression *fake_assign_expr(struct symbol *sym)
{			
	struct expression *e_assign, *e_symbol;

	e_assign = alloc_expression(sym->initializer->pos, EXPR_ASSIGNMENT);
	e_symbol = alloc_expression(sym->initializer->pos, EXPR_SYMBOL);
	e_assign->op = (int)'=';
	e_symbol->symbol = sym;
	e_symbol->symbol_name = sym->ident;
	e_assign->left = e_symbol;
	e_assign->right = sym->initializer;
	return e_assign;
}

static void split_declaration(struct symbol_list *sym_list)
{
	struct symbol *sym;
	struct expression *assign, *tmp;

	FOR_EACH_PTR(sym_list, sym) {
		__pass_to_client(sym, DECLARATION_HOOK);
		__split_expr(sym->initializer);
		if(sym->initializer) {
			assign = fake_assign_expr(sym);
			__pass_to_client(assign, ASSIGNMENT_HOOK);
			tmp = strip_expr(assign->right);
			if (tmp->type == EXPR_CALL)
				__pass_to_client(assign, CALL_ASSIGNMENT_HOOK);
		}
		split_sym(sym);
	} END_FOR_EACH_PTR(sym);
}

static void split_functions(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		struct symbol *base_type;
		base_type = get_base_type(sym);
		if (sym->type == SYM_NODE && base_type->type == SYM_FN) {
			cur_func_sym = sym;
			if (base_type->stmt)
				line_func_start = base_type->stmt->pos.line;
			if (sym->ident)
				cur_func = sym->ident->name;
			__smatch_lineno = sym->pos.line;
			sm_debug("new function:  %s\n", cur_func);
			if (option_two_passes) {
				__unnullify_path();
				loop_num = 0;
				final_pass = 0;
				__pass_to_client(sym, FUNC_DEF_HOOK);
				__split_statements(base_type->stmt);
				nullify_path();
			}
			__unnullify_path();
			loop_num = 0;
			final_pass = 1;
			__pass_to_client(sym, FUNC_DEF_HOOK);
			__split_statements(base_type->stmt);
			__pass_to_client(sym, END_FUNC_HOOK);
			cur_func = NULL;
			line_func_start = 0;
			clear_all_states();
			free_data_info_allocs();
			free_expression_stack(&switch_expr_stack);
		} else {
			__pass_to_client(sym, BASE_HOOK);
		}
	} END_FOR_EACH_PTR(sym);
	__pass_to_client_no_data(END_FILE_HOOK);
}

void smatch (int argc, char **argv) 
{

	struct string_list *filelist = NULL;
	struct symbol_list *sym_list;
	
	if (argc < 2) {
		printf("Usage:  smatch [--debug] <filename.c>\n");
		exit(1);
	}
	sparse_initialize(argc, argv, &filelist);
	FOR_EACH_PTR_NOTAG(filelist, filename) {
		int len;

		pathname = get_current_dir_name();
		if (pathname) {
			len = strlen(pathname) + 1 + strlen(filename) + 1;
			full_filename = malloc(len);
			snprintf(full_filename, len, "%s/%s", pathname, filename);
		} else {
			full_filename = filename;
		}

		sym_list = __sparse(filename);
		split_functions(sym_list);

		if (pathname) {
			free(full_filename);
			free(pathname);
		}
	} END_FOR_EACH_PTR_NOTAG(filename);
}
