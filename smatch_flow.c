/*
 * sparse/check_string_ret.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include "token.h"
#include "smatch.h"

#define KERNEL

int __smatch_lineno = 0;
int __negate = 0;
int __ors = 0;
int __ands = 0;

static char *filename;
static char *cur_func;
static int line_func_start;
static unsigned int path_id = 0;
static unsigned int path_id_next = 1;

char *get_filename() {	return filename; }
char *get_function() { return cur_func; }
int get_lineno() { return __smatch_lineno; }
int get_func_pos() { return __smatch_lineno - line_func_start; }

static void split_statements(struct statement *stmt);
static void split_symlist(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list);
static void split_expr(struct expression *expr);

unsigned int get_path_id() 
{
	return path_id;
}

static unsigned int split_path_id()
{
	int tmp = path_id;

	path_id = path_id_next++;
	return tmp;
}

static void restore_path_id(int old_id)
{
	path_id = old_id;
}


static int is_logical_and(struct expression *expr)
{
	/* If you have if (!(a && b)) smatch translates that to 
	 * if (!a || !b).  Logically those are the same.
	 */

	if ((!__negate && expr->op == SPECIAL_LOGICAL_AND) ||
	    (__negate && expr->op == SPECIAL_LOGICAL_OR))
		return 1;
	return 0;
}

static int is_logical_or(struct expression *expr)
{
	if ((!__negate && expr->op == SPECIAL_LOGICAL_OR) ||
	     (__negate && expr->op == SPECIAL_LOGICAL_AND))
		return 1;
	return 0;
}

static void inc_ands_ors(struct expression *expr)
{
	if (is_logical_and(expr))
		__ands++;
	else if (is_logical_or(expr))
		__ors++;
}

static void dec_ands_ors(struct expression *expr)
{
	if (is_logical_and(expr))
		__ands--;
	else if (is_logical_or(expr))
		__ors--;
}

void split_conditions(struct expression *expr)
{
	/*
	 * If you have if ((a || a = foo()), we know a is
	 * non-null on the true state because 'a' is checked in both and
	 * groups.  __ors_reached helps with that.  Unfortunately
	 * smatch doesn't handle stuff like if (a && a = foo())
	 * because it never occured to me that people would do that...
	 */

	static int __ors_reached;
  
	if (expr->type == EXPR_LOGICAL) {
		unsigned int path_orig;

		inc_ands_ors(expr);
		path_orig = split_path_id();
		__split_false_states_mini();

		split_conditions(expr->left);
		
		if (is_logical_and(expr)) {
			split_path_id();
			__pop_false_states_mini();
			split_conditions(expr->right);
		} else if (is_logical_or(expr)) {
			if (!__ors_reached) {
				__ors_reached++;
				__first_and_clump();
			} else
				__merge_and_clump();
			
			split_path_id();
			__use_false_states_mini();
			split_conditions(expr->right);
		}
		dec_ands_ors(expr);
		
		if (__ands + __ors == 0) {
			__merge_and_clump();
			__use_and_clumps();
			__ors_reached = 0;
		}

		restore_path_id(path_orig);
		return;
	} else if (expr->type == EXPR_PREOP && expr->op == '!') {
		__negate = (__negate +  1)%2;
		split_conditions(expr->unop);
		__negate = (__negate +  1)%2;
		return;
	} else if (expr->type == EXPR_PREOP && expr->op == '(') {
		split_conditions(expr->unop);
#ifdef KERNEL
	} else if (expr->type == EXPR_CALL) {
		struct expression *arg;

		if (expr->fn->type != EXPR_SYMBOL || 
		    strcmp("__builtin_expect", expr->fn->symbol_name->name)) {
			__pass_to_client(expr, CONDITION_HOOK);	
			split_expr(expr);
			return;
		}
		arg = first_ptr_list((struct ptr_list *) expr->args);
		if (arg->op == '!' && arg->unop->op == '!' 
		    && arg->unop->unop->op == '(')
			split_conditions(arg->unop->unop->unop);
		else {
			__pass_to_client(expr, CONDITION_HOOK);	
			split_expr(expr);
			return;
		}
#endif 
	} else {
		__pass_to_client(expr, CONDITION_HOOK);	
		split_expr(expr);
	}
}

static void split_expr(struct expression *expr)
{
	if (!expr)
		return;

	__smatch_lineno = expr->pos.line;
	__pass_to_client(expr, EXPR_HOOK);

	//printf("Debug expr_type %d\n", expr->type);

	switch (expr->type) {
	case EXPR_PREOP: 
	case EXPR_POSTOP:
		if (expr->op == '*')
			__pass_to_client(expr, DEREF_HOOK);
		split_expr(expr->unop);
		return;
	case EXPR_STATEMENT:
		split_statements(expr->statement);
		return;
	case EXPR_BINOP: 
	case EXPR_COMMA:
	case EXPR_COMPARE:
	case EXPR_LOGICAL:
	case EXPR_ASSIGNMENT:
		if (expr->type == EXPR_ASSIGNMENT)
			__pass_to_client(expr, ASSIGNMENT_HOOK);
		split_expr(expr->left);
		split_expr(expr->right);
		if (expr->type == EXPR_ASSIGNMENT)
			__pass_to_client(expr, ASSIGNMENT_AFTER_HOOK);
		return;
	case EXPR_DEREF:
		__pass_to_client(expr, DEREF_HOOK);
		split_expr(expr->deref);
		return;
	case EXPR_SLICE:
		split_expr(expr->base);
		return;
	case EXPR_CAST:
	case EXPR_SIZEOF:
		split_expr(expr->cast_expression);
		return;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		__split_true_false_paths();
		__pass_to_client(expr->conditional, WHOLE_CONDITION_HOOK);
		split_conditions(expr->conditional);
		__use_true_states();
		split_expr(expr->cond_true);
		__use_false_states();
		split_expr(expr->cond_false);
		__merge_true_states();
		return;
	case EXPR_CALL:
		__pass_to_client(expr, FUNCTION_CALL_HOOK);
		split_expr(expr->fn);
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
		split_expr(expr->ident_expression);
		return;
	case EXPR_INDEX:
		split_expr(expr->idx_expression);
		return;
	case EXPR_POS:
		split_expr(expr->init_expr);
		return;
	default:
		return;
	};
}

static int is_forever_loop(struct expression *expr)
{
	
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
	unsigned int path_orig;

	split_statements(stmt->iterator_pre_statement);

	__split_true_false_paths();
	__push_continues();
	__push_breaks();
	__pass_to_client(stmt->iterator_pre_condition, WHOLE_CONDITION_HOOK);
	split_conditions(stmt->iterator_pre_condition);
	path_orig = split_path_id();
	__use_true_states();

	split_statements(stmt->iterator_statement);
	split_statements(stmt->iterator_post_statement);
	if (is_forever_loop(stmt->iterator_pre_condition)) {
		__pop_continues();
		__pop_false_states();
		nullify_path();		
	} else {
		__merge_false_states();
		__merge_continues();
	}
	__merge_breaks();
	restore_path_id(path_orig);
}

/*
 * Post loops are do {} while();
 */
static void handle_post_loop(struct statement *stmt)
{
	__push_continues();
	__push_breaks();
	split_statements(stmt->iterator_statement);
	
	__split_true_false_paths();
	__pass_to_client(stmt->iterator_post_condition, WHOLE_CONDITION_HOOK);
	split_conditions(stmt->iterator_post_condition);
	/* It would prossibly be cleaner to make this all one function */
	__use_true_states();
	__use_false_states();
	__pop_true_states();

	if (is_forever_loop(stmt->iterator_post_condition)) {
		__pop_continues();
		nullify_path();
	} else {
		__merge_continues();
	}
	__merge_breaks();
}

static void split_statements(struct statement *stmt)
{
	unsigned int path_orig;

	if (!stmt)
		return;
	
	__smatch_lineno = stmt->pos.line;
	__pass_to_client(stmt, STMT_HOOK);

	switch (stmt->type) {
	case STMT_DECLARATION:
		__pass_declarations_to_client(stmt->declaration);
		split_symlist(stmt->declaration);
		return;
	case STMT_RETURN:
		__pass_to_client(stmt, RETURN_HOOK);
		split_expr(stmt->ret_value);
		nullify_path();
		return;
	case STMT_EXPRESSION:
		split_expr(stmt->expression);
		return;
	case STMT_COMPOUND: {
		struct statement *s;
		FOR_EACH_PTR(stmt->stmts, s) {
			split_statements(s);
		} END_FOR_EACH_PTR(s);
		return;
	}
	case STMT_IF:
		__split_true_false_paths();
		__pass_to_client(stmt->if_conditional, WHOLE_CONDITION_HOOK);
		split_conditions(stmt->if_conditional);
		path_orig = split_path_id();
		__use_true_states();
		split_statements(stmt->if_true);
		split_path_id();
		__use_false_states();
		split_statements(stmt->if_false);
		__merge_true_states();
		restore_path_id(path_orig);
		return;
	case STMT_ITERATOR:
		if (stmt->iterator_pre_condition)
			handle_pre_loop(stmt);
		else if (stmt->iterator_post_condition)
			handle_post_loop(stmt);
		return;
	case STMT_SWITCH:
		split_expr(stmt->switch_expression);
		__save_switch_states();
		__push_default();
		__push_breaks();
		nullify_path();
		path_orig = get_path_id();
		split_statements(stmt->switch_statement);
		if (!__pop_default())
			__merge_switches();
		__pop_switches();
		__merge_breaks();
		restore_path_id(path_orig);
		return;
	case STMT_CASE:
		split_path_id();
		__merge_switches();
		if (!stmt->case_expression)
			__set_default();
		split_expr(stmt->case_expression);
		split_expr(stmt->case_to);
		split_statements(stmt->case_statement);
		return;
	case STMT_LABEL:
		if (stmt->label && 
		    stmt->label->type == SYM_LABEL && 
		    stmt->label->ident) {
			__merge_gotos(stmt->label->ident->name);
		}
		split_statements(stmt->label_statement);
		return;
	case STMT_GOTO:
		split_expr(stmt->goto_expression);
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
		split_expr(stmt->asm_string);
		//split_expr(stmt->asm_outputs);
		//split_expr(stmt->asm_inputs);
		//split_expr(stmt->asm_clobbers);
		return;
	case STMT_CONTEXT:
		return;
	case STMT_RANGE:
		split_expr(stmt->range_expression);
		split_expr(stmt->range_low);
		split_expr(stmt->range_high);
		return;
	}
}

static void split_expr_list(struct expression_list *expr_list)
{
	struct expression *expr;
	FOR_EACH_PTR(expr_list, expr) {
		split_expr(expr);
	} END_FOR_EACH_PTR(expr);
}


static void split_sym(struct symbol *sym)
{
	if (!sym)
		return;
	if (!(sym->namespace & NS_SYMBOL))
		return;

	__pass_to_client(sym, SYM_HOOK);
	split_statements(sym->stmt);
	split_expr(sym->array_size);
	split_symlist(sym->arguments);
	split_symlist(sym->symbol_list);
	split_statements(sym->inline_stmt);
	split_symlist(sym->inline_symbol_list);
	split_expr(sym->initializer);
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
			__pass_to_client(sym, FUNC_DEF_HOOK);
			split_statements(base_type->stmt);
			__pass_to_client(sym, END_FUNC_HOOK);
			cur_func = NULL;
			line_func_start = 0;
			clear_all_states();

		} else {
			__pass_to_client(sym, BASE_HOOK);
		}
	} END_FOR_EACH_PTR(sym);
}

void smatch (int argc, char **argv) 
{

	struct string_list *filelist = NULL;
	struct symbol_list *sym_list;
	
	if (argc < 2) {
		printf("Usage:  smatch <filename.c>\n");
		exit(1);
	}
	sparse_initialize(argc, argv, &filelist);
	FOR_EACH_PTR_NOTAG(filelist, filename) {
		sym_list = __sparse(filename);
		split_functions(sym_list);
	} END_FOR_EACH_PTR_NOTAG(filename);
}
