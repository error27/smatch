/*
 * Linearize - walk the statement tree (but _not_ the expressions)
 * to generate a linear version of it and the basic blocks. 
 *
 * NOTE! We're not interested in the actual sub-expressions yet,
 * even though they can generate conditional branches and
 * subroutine calls. That's all "local" behaviour.
 *
 * Copyright (C) 2004 Linus Torvalds
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"

static struct entrypoint *alloc_entrypoint(void)
{
	return __alloc_entrypoint(0);
}

static struct basic_block *alloc_basic_block(void)
{
	return __alloc_basic_block(0);
}

static void show_bb(struct basic_block *bb)
{
	struct statement *stmt;

	printf("bb: %p%s\n", bb, bb->this ? "" : " UNREACHABLE!!");
	FOR_EACH_PTR(bb->stmts, stmt) {
		show_statement(stmt);
	} END_FOR_EACH_PTR;

	if (bb->next) {
		printf("\tgoto\t\t.L%p\n", bb->next->bb_target);
	} else {
		printf("\tEND\n");
	}
	printf("\n");
}

static void show_entry(struct entrypoint *ep)
{
	struct symbol *sym;
	struct basic_block *bb;

	printf("ep %p: %s\n", ep, show_ident(ep->name->ident));

	FOR_EACH_PTR(ep->syms, sym) {
		printf("   sym: %p %s\n", sym, show_ident(sym->ident));
	} END_FOR_EACH_PTR;

	printf("\n");

	FOR_EACH_PTR(ep->bbs, bb) {
		show_bb(bb);
	} END_FOR_EACH_PTR;

	printf("\n");
}

#define bb_reachable(bb) ((bb)->this != NULL)

static struct basic_block * new_basic_block(struct entrypoint *ep, struct symbol *owner)
{
	struct basic_block *bb;

	if (!owner) {
		static struct basic_block unreachable;
		return &unreachable;
	}
		
	bb = alloc_basic_block();
	add_bb(&ep->bbs, bb);
	bb->this = owner;
	if (owner->bb_target)
		warn(owner->pos, "Symbol already has a basic block %p", owner->bb_target);
	owner->bb_target = bb;
	return bb;
}

static void linearize_simple_statement(struct entrypoint *ep, struct statement *stmt)
{
	add_statement(&ep->active->stmts, stmt);
}

static void add_label(struct entrypoint *ep, struct symbol *sym)
{
	struct basic_block *new_bb = new_basic_block(ep, sym);

	ep->active->next = sym;
	ep->active = new_bb;
}

static void set_unreachable(struct entrypoint *ep)
{
	ep->active = new_basic_block(ep, NULL);
}

void linearize_statement(struct entrypoint *ep, struct statement *stmt)
{
	if (!stmt)
		return;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_ASM:
	case STMT_EXPRESSION:
		linearize_simple_statement(ep, stmt);
		break;

	case STMT_RETURN:
		linearize_simple_statement(ep, stmt);
		set_unreachable(ep);
		break;

	case STMT_CASE: {
		add_label(ep, stmt->case_label);
		linearize_statement(ep, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		add_label(ep, stmt->label_identifier);
		linearize_statement(ep, stmt->label_statement);
		break;
	}

	case STMT_GOTO: {
		ep->active->next = stmt->goto_label;
		set_unreachable(ep);
		break;
	}

	case STMT_COMPOUND: {
		struct statement *s;
		concat_symbol_list(stmt->syms, &ep->syms);
		FOR_EACH_PTR(stmt->stmts, s) {
			linearize_statement(ep, s);
		} END_FOR_EACH_PTR;
		break;
	}

	/*
	 * This could take 'likely/unlikely' into account, and
	 * switch the arms around appropriately..
	 */
	case STMT_IF: {
		struct symbol *target;
		struct statement *goto_bb;
		struct basic_block *if_block;
		struct expression *cond = stmt->if_conditional;

		if (cond->type == EXPR_VALUE) {
			struct statement *always = stmt->if_true;
			struct statement *never = stmt->if_false;

			if (!cond->value) {
				never = always;
				always = stmt->if_false;
			}
			if (always)
				linearize_statement(ep, always);
			if (never) {
				struct basic_block *bb = ep->active;
				set_unreachable(ep);
				linearize_statement(ep, never);

				/*
				 * If the "never case" is reachable some other
				 * way, we need to merge the old always case
				 * with the fallthrough of the never case.
				 */
				if (bb_reachable(ep->active)) {
					struct symbol *merge = alloc_symbol(never->pos, SYM_LABEL);
					add_label(ep, merge);
					bb->next = merge;
					ep->active = new_basic_block(ep, merge);
					break;
				}

				/* Otherwise we just continue with the old always case.. */
				ep->active = bb;
			}
			break;
		}
			

		target = alloc_symbol(stmt->pos, SYM_LABEL);
		goto_bb = alloc_statement(stmt->pos, STMT_CONDFALSE);
		goto_bb->bb_conditional = cond;
		goto_bb->bb_target = target;

		linearize_simple_statement(ep, goto_bb);
		linearize_statement(ep, stmt->if_true);

		if_block = ep->active;
		add_label(ep, target);
		
		if (stmt->if_false) {
			struct symbol *else_target = alloc_symbol(stmt->pos, SYM_LABEL);
			if_block->next = else_target;
			linearize_statement(ep, stmt->if_false);
			add_label(ep, else_target);
		}
		break;
	}

	case STMT_SWITCH: {
		struct symbol *sym;
		struct statement *switch_value;

		/* Create the "head node" */
		switch_value = alloc_statement(stmt->pos, STMT_MULTIVALUE);
		switch_value->expression = stmt->switch_expression;
		linearize_simple_statement(ep, switch_value);

		/* Create all the sub-jumps */
		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct statement *sw_bb = alloc_statement(case_stmt->pos, STMT_MULTIJMP);
			sw_bb->multi_from = case_stmt->case_expression;
			sw_bb->multi_to = case_stmt->case_to;
			sw_bb->multi_target = sym;
			linearize_simple_statement(ep, sw_bb);
		} END_FOR_EACH_PTR;

		/* Default fall-through case */
		ep->active->next = stmt->switch_break;
		set_unreachable(ep);

		/* And linearize the actual statement */
		linearize_statement(ep, stmt->switch_statement);

		/* ..then tie it all together at the end.. */
		add_label(ep, stmt->switch_break);
		break;
	}

	case STMT_ITERATOR: {
		struct statement  *pre_statement = stmt->iterator_pre_statement;
		struct expression *pre_condition = stmt->iterator_pre_condition;
		struct statement  *statement = stmt->iterator_statement;
		struct statement  *post_statement = stmt->iterator_post_statement;
		struct expression *post_condition = stmt->iterator_post_condition;
		struct symbol *loop_top = NULL, *loop_bottom = NULL;

		concat_symbol_list(stmt->iterator_syms, &ep->syms);
		linearize_statement(ep, pre_statement);
		if (pre_condition) {
			if (pre_condition->type == EXPR_VALUE) {
				if (!pre_condition->value) {
					loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
					ep->active->next = loop_bottom;
					set_unreachable(ep);
				}
			} else {
				struct statement *pre_cond = alloc_statement(stmt->pos, STMT_CONDFALSE);
				loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
				pre_cond->bb_conditional = pre_condition;
				pre_cond->bb_target = loop_bottom;
				linearize_simple_statement(ep, pre_cond);
			}
		}

		if (!post_condition || post_condition->type != EXPR_VALUE || post_condition->value) {
			loop_top = alloc_symbol(stmt->pos, SYM_LABEL);
			add_label(ep, loop_top);
		}

		linearize_statement(ep, statement);

		if (stmt->iterator_continue->used)
			add_label(ep, stmt->iterator_continue);

		linearize_statement(ep, post_statement);

		if (!post_condition) {
			ep->active->next = loop_top;
			set_unreachable(ep);
		} else {
			if (post_condition->type != EXPR_VALUE || post_condition->value) {
				struct statement *post_cond = alloc_statement(stmt->pos, STMT_CONDTRUE);
				post_cond->bb_conditional = post_condition;
				post_cond->bb_target = loop_top;
				linearize_simple_statement(ep, post_cond);
			}
		}

		if (stmt->iterator_break->used)
			add_label(ep, stmt->iterator_break);
		if (loop_bottom)
			add_label(ep, loop_bottom);
		break;
	}

	default:
		break;
	}
}

void linearize_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return;
	base_type = sym->ctype.base_type;
	if (!base_type)
		return;
	if (base_type->type == SYM_FN) {
		if (base_type->stmt) {
			struct entrypoint *ep = alloc_entrypoint();

			ep->name = sym;
			ep->active = new_basic_block(ep, sym);
			concat_symbol_list(base_type->arguments, &ep->syms);
			linearize_statement(ep, base_type->stmt);
			show_entry(ep);
		}
	}
}
