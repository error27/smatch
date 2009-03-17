/*
 * sparse/smatch_flow.c
 *
 * Copyright (C) 2006,2008 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include "token.h"
#include "smatch.h"

int __smatch_lineno = 0;

static char *filename;
static char *cur_func;
static int line_func_start;

char *get_filename() {	return filename; }
char *get_function() { return cur_func; }
int get_lineno() { return __smatch_lineno; }
int get_func_pos() { return __smatch_lineno - line_func_start; }

static void split_symlist(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list);

unsigned int __get_allocations();

int option_assume_loops = 0;

void __split_expr(struct expression *expr)
{
	if (!expr)
		return;

	// printf("%d Debug expr_type %d\n", get_lineno(), expr->type);

	__smatch_lineno = expr->pos.line;
	__pass_to_client(expr, EXPR_HOOK);

	switch (expr->type) {
	case EXPR_PREOP: 
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
	case EXPR_ASSIGNMENT:
		if (expr->type == EXPR_ASSIGNMENT)
			__pass_to_client(expr, ASSIGNMENT_HOOK);
		__split_expr(expr->left);
		__split_expr(expr->right);
		if (expr->type == EXPR_ASSIGNMENT)
			__pass_to_client(expr, ASSIGNMENT_AFTER_HOOK);
		return;
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
		__pass_to_client(expr, FUNCTION_CALL_HOOK);
		__split_expr(expr->fn);
		split_expr_list(expr->args);
		__pass_to_client(expr, FUNCTION_CALL_AFTER_HOOK);
#ifdef KERNEL
		if (expr->fn->type == EXPR_SYMBOL && 
		    !strcmp(expr->fn->symbol_name->name, "panic"))
			nullify_path();
#endif 
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

/*
 * Pre Loops are while and for loops.
 */

static void handle_pre_loop(struct statement *stmt)
{
	int once_through; /* we go through the loop at least once */

	__split_statements(stmt->iterator_pre_statement);

	once_through = known_condition_true(stmt->iterator_pre_condition);
	if (option_assume_loops)
		once_through = 1;

	__push_continues();
	__push_breaks();

	__split_whole_condition(stmt->iterator_pre_condition);

	__split_statements(stmt->iterator_statement);
	__warn_on_silly_pre_loops();	
	if (is_forever_loop(stmt)) {
		__pop_false_only_stack();
		/* forever loops don't have an iterator_post_statement */
		__pop_continues();
		__pop_false_states();
		__use_breaks();
	} else if (once_through) {
		__merge_continues();
		__split_statements(stmt->iterator_post_statement);
		__pop_false_states();
		__use_false_only_stack();
		__merge_breaks();
	} else {
		__merge_continues();
		__split_statements(stmt->iterator_post_statement);
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
	__push_continues();
	__push_breaks();
	__split_statements(stmt->iterator_statement);
	if (is_forever_loop(stmt)) {
		__pop_continues();
		__use_breaks();

	} else {
		__merge_continues();
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
#ifdef KERNEL
	return;
#endif
	if (__path_is_null()) {
		switch(stmt->type) {
		case STMT_COMPOUND: /* after a switch before a case stmt */
		case STMT_CASE:
		case STMT_LABEL:
		case STMT_DECLARATION: /* switch(x) { int a; case foo: ... */
			break;
		default:
			smatch_msg("unreachable code. %d", stmt->type);
		}
	}
}

void __split_statements(struct statement *stmt)
{
	if (!stmt)
		return;
	
	if (__get_allocations() > MAXSMSTATES) {
		static char *printed = NULL;

		if (printed != cur_func)
			smatch_msg("Function too big.  Giving up.");
		printed = cur_func;
		return;
	}

	__smatch_lineno = stmt->pos.line;
	print_unreached(stmt);
	__pass_to_client(stmt, STMT_HOOK);

	switch (stmt->type) {
	case STMT_DECLARATION:
		__pass_declarations_to_client(stmt->declaration);
		split_symlist(stmt->declaration);
		return;
	case STMT_RETURN:
		__split_expr(stmt->ret_value);
		__pass_to_client(stmt, RETURN_HOOK);
		nullify_path();
		return;
	case STMT_EXPRESSION:
		__split_expr(stmt->expression);
		return;
	case STMT_COMPOUND: {
		struct statement *s;
		FOR_EACH_PTR(stmt->stmts, s) {
			__split_statements(s);
		} END_FOR_EACH_PTR(s);
		return;
	}
	case STMT_IF:
		if (known_condition_true(stmt->if_conditional)) {
			smatch_msg("info: this condition is true.");
			__split_statements(stmt->if_true);
			return;
		}
		if (known_condition_false(stmt->if_conditional)) {
			smatch_msg("info: this condition is false.");
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
		__save_switch_states();
		__push_default();
		__push_breaks();
		__split_statements(stmt->switch_statement);
		if (!__pop_default())
			__merge_switches();
		__pop_switches();
		__merge_breaks();
		return;
	case STMT_CASE:
		__merge_switches();
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

static void split_functions(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		struct symbol *base_type;
		base_type = get_base_type(sym);
		if (sym->type == SYM_NODE && base_type->type == SYM_FN) {
			if (base_type->stmt)
				line_func_start = base_type->stmt->pos.line;
			if (sym->ident)
				cur_func = sym->ident->name;
			__smatch_lineno = sym->pos.line;
			SM_DEBUG("new function:  %s\n", cur_func);
			__unnullify_path();
			__pass_to_client(sym, FUNC_DEF_HOOK);
			__split_statements(base_type->stmt);
			__pass_to_client(sym, END_FUNC_HOOK);
			cur_func = NULL;
			line_func_start = 0;
			clear_all_states();

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
		sym_list = __sparse(filename);
		split_functions(sym_list);
	} END_FOR_EACH_PTR_NOTAG(filename);
}
