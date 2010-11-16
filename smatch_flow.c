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
#include "smatch_slist.h"

int final_pass;

static int __smatch_lineno = 0;

static const char *filename;
static char *pathname;
static char *full_filename;
static char *cur_func;
static int line_func_start;
static int loop_count;
int __expr_stmt_count;
static struct expression_list *switch_expr_stack = NULL;

struct expression_list *big_expression_stack;
struct statement_list *big_statement_stack;
int __in_pre_condition = 0;
int __bail_on_rest_of_function = 0;
char *get_function(void) { return cur_func; }
int get_lineno(void) { return __smatch_lineno; }
int get_func_pos(void) { return __smatch_lineno - line_func_start; }
int inside_loop(void) { return !!loop_count; }
int in_expression_statement(void) { return !!__expr_stmt_count; }

static void split_symlist(struct symbol_list *sym_list);
static void split_declaration(struct symbol_list *sym_list);
static void split_expr_list(struct expression_list *expr_list);
static void add_inline_function(struct symbol *sym);

int option_assume_loops = 0;
int option_known_conditions = 0;
int option_two_passes = 0;
struct symbol *cur_func_sym = NULL;

const char *get_filename(void)
{
	if (option_full_path)
		return full_filename;
	return filename;
}

static void set_position(struct expression *expr)
{
	int len;
	static int prev_stream = -1;

	__smatch_lineno = expr->pos.line;

	if (expr->pos.stream == prev_stream)
		return;

	filename = stream_name(expr->pos.stream);
       
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

void __split_expr(struct expression *expr)
{
	if (!expr)
		return;

	// sm_msg(" Debug expr_type %d %s", expr->type, show_special(expr->op));

	push_expression(&big_expression_stack, expr);
	set_position(expr);
	__pass_to_client(expr, EXPR_HOOK);

	switch (expr->type) {
	case EXPR_PREOP: 
		if (expr->op == '*')
			__pass_to_client(expr, DEREF_HOOK);
	case EXPR_POSTOP:
		__pass_to_client(expr, OP_HOOK);
		__split_expr(expr->unop);
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
		__split_expr(expr->right);		
		break;
	case EXPR_ASSIGNMENT: {
		struct expression *tmp;

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
		__pass_to_client(expr, ASSIGNMENT_HOOK);
		tmp = strip_expr(expr->right);
		if (tmp->type == EXPR_CALL)
			__pass_to_client(expr, CALL_ASSIGNMENT_HOOK);
		if (get_macro_name(&tmp->pos))
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
		__split_expr(expr->cast_expression);
		break;
	case EXPR_SIZEOF:
		/* there isn't anything to pass a client from inside a sizeof() */
		break;
	case EXPR_CONDITIONAL:
	case EXPR_SELECT:
		__split_whole_condition(expr->conditional);
		__split_expr(expr->cond_true);
		__push_true_states();
		__use_false_states();
		__split_expr(expr->cond_false);
		__merge_true_states();
		break;
	case EXPR_CALL:
		split_expr_list(expr->args);
		__split_expr(expr->fn);
		if (is_inline_func(expr->fn))
			add_inline_function(expr->fn->symbol);
		__pass_to_client(expr, FUNCTION_CALL_HOOK);
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
		__save_gotos(loop_name);
		/* forever loops don't have an iterator_post_statement */
		__discard_continues();
		__discard_false_states();
		__use_breaks();
	} else {
		__merge_continues();
		unchanged = __iterator_unchanged(extra_sm);
		__split_stmt(stmt->iterator_post_statement);
		__save_gotos(loop_name);
		__split_whole_condition(stmt->iterator_pre_condition);
		nullify_path();
		__merge_false_states();
		if (once_through) {
			__discard_false_states();
		} else {
			__merge_false_states();

		}
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
		if(sym->initializer)
			sm_msg("info: '%s' is not actually initialized (unreached code).", 
				(sym->ident ? sym->ident->name : "this variable"));
	} END_FOR_EACH_PTR(sym);
}

static void print_unreached(struct statement *stmt)
{

	static int print = 1;

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

void __split_stmt(struct statement *stmt)
{
	if (!stmt)
		return;

	if (out_of_memory() || __bail_on_rest_of_function) {
		static char *printed = NULL;

		if (printed != cur_func)
			sm_msg("Function too hairy.  Giving up.");
		printed = cur_func;
		return;
	}

	add_ptr_list(&big_statement_stack, stmt);
	free_expression_stack(&big_expression_stack);
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
		struct statement *tmp;

		if (!last_stmt)
			last_stmt = last_ptr_list((struct ptr_list *)stmt->stmts);
		__push_scope_hooks();
		FOR_EACH_PTR(stmt->stmts, tmp) {
			__split_stmt(tmp);
		} END_FOR_EACH_PTR(tmp);
		__call_scope_hooks();
		return;
	}
	case STMT_IF:
		if (known_condition_true(stmt->if_conditional)) {
			__split_stmt(stmt->if_true);
			return;
		}
		if (known_condition_false(stmt->if_conditional)) {
			__split_stmt(stmt->if_false);
			return;
		}
		if (option_known_conditions && 
		    implied_condition_true(stmt->if_conditional)) {
			sm_info("this condition is true.");
			__split_stmt(stmt->if_true);
			return;
		}
		if (option_known_conditions &&
		    implied_condition_false(stmt->if_conditional)) {
			sm_info("this condition is false.");
			__split_stmt(stmt->if_false);
			return;
		}
		__split_whole_condition(stmt->if_conditional);
		__split_stmt(stmt->if_true);
		if (empty_statement(stmt->if_true) &&
			last_stmt_on_same_line() &&
			!get_macro_name(&stmt->if_true->pos))
			sm_msg("warn: if();");
		__push_true_states();
		__use_false_states();
		__split_stmt(stmt->if_false);
		__merge_true_states();
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
		__split_stmt(stmt->switch_statement);
		if (!__pop_default())
			__merge_switches(top_expression(switch_expr_stack),
				      NULL);
		__discard_switches();
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
		__split_stmt(stmt->case_statement);
		return;
	case STMT_LABEL:
		if (stmt->label && 
		    stmt->label->type == SYM_LABEL && 
		    stmt->label->ident) {
			loop_count = 1000000;
			__merge_gotos(stmt->label->ident->name);
		}
		__split_stmt(stmt->label_statement);
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
		__pass_to_client(stmt, ASM_HOOK);
		__split_expr(stmt->asm_string);
		split_asm_constraints(stmt->asm_outputs);
		split_asm_constraints(stmt->asm_inputs);
		split_asm_constraints(stmt->asm_clobbers);
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

static void do_initializer_stuff(struct symbol *sym)
{
	struct expression *assign;

	if(!sym->initializer)
		return;
	assign = fake_assign_expr(sym);
	__split_expr(assign);
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

static void split_function(struct symbol *sym)
{
	struct symbol *base_type = get_base_type(sym);

	cur_func_sym = sym;
	if (base_type->stmt)
		line_func_start = base_type->stmt->pos.line;
	if (sym->ident)
		cur_func = sym->ident->name;
	__smatch_lineno = sym->pos.line;
	last_stmt = NULL;
	loop_count = 0;
	sm_debug("new function:  %s\n", cur_func);
	if (option_two_passes) {
		__unnullify_path();
		loop_num = 0;
		final_pass = 0;
		__pass_to_client(sym, FUNC_DEF_HOOK);
		__split_stmt(base_type->stmt);
		__split_stmt(base_type->inline_stmt);
		nullify_path();
	}
	__unnullify_path();
	loop_num = 0;
	final_pass = 1;
	__pass_to_client(sym, FUNC_DEF_HOOK);
	__split_stmt(base_type->stmt);
	__split_stmt(base_type->inline_stmt);
	__pass_to_client(sym, END_FUNC_HOOK);
	cur_func = NULL;
	line_func_start = 0;
	clear_all_states();
	free_data_info_allocs();
	free_expression_stack(&switch_expr_stack);
	__free_ptr_list((struct ptr_list **)&big_statement_stack);
	__bail_on_rest_of_function = 0;
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

static void split_functions(struct symbol_list *sym_list)
{
	struct symbol *sym;

	FOR_EACH_PTR(sym_list, sym) {
		if (sym->type == SYM_NODE && get_base_type(sym)->type == SYM_FN) {
			split_function(sym);
			process_inlines();
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
	char *file;
	
	if (argc < 2) {
		printf("Usage:  smatch [--debug] <filename.c>\n");
		exit(1);
	}
	sparse_initialize(argc, argv, &filelist);
	FOR_EACH_PTR_NOTAG(filelist, file) {
		sym_list = sparse_keep_tokens(file);
		split_functions(sym_list);
	} END_FOR_EACH_PTR_NOTAG(file);
}
