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

static struct basic_block * linearize_statement(struct entrypoint *ep,
	struct basic_block *bb,
	struct statement *stmt)
{
	if (!stmt)
		return bb;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_ASM:
	case STMT_EXPRESSION:
		add_statement(&bb->stmts, stmt);
		break;

	case STMT_RETURN:
		add_statement(&bb->stmts, stmt);
		bb = new_basic_block(ep, NULL);
		break;

	case STMT_CASE: {
		struct symbol *sym = stmt->case_label;
		struct basic_block *new_bb = new_basic_block(ep, sym);

		bb->next = sym;
		bb = linearize_statement(ep, new_bb, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		struct symbol *sym = stmt->label_identifier;
		struct basic_block *new_bb = new_basic_block(ep, sym);

		bb->next = sym;

		bb = linearize_statement(ep, new_bb, stmt->label_statement);
		break;
	}

	case STMT_GOTO: {
		struct basic_block *new_bb = new_basic_block(ep, NULL);
		struct symbol *sym = stmt->goto_label;

		bb->next = sym;
		bb = new_bb;
		break;
	}

	case STMT_COMPOUND: {
		struct statement *s;
		concat_symbol_list(stmt->syms, &ep->syms);
		FOR_EACH_PTR(stmt->stmts, s) {
			bb = linearize_statement(ep, bb, s);
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
		struct basic_block *last_bb;
		struct expression *cond = stmt->if_conditional;

		if (cond->type == EXPR_VALUE) {
			struct statement *always = stmt->if_true;
			struct statement *never = stmt->if_false;
			if (!cond->value) {
				never = always;
				always = stmt->if_false;
			}
			if (always)
				bb = linearize_statement(ep, bb, always);
			if (never) {
				struct basic_block *n = new_basic_block(ep, NULL);
				n = linearize_statement(ep, n, never);
				if (bb_reachable(n)) {
					struct symbol *merge = alloc_symbol(never->pos, SYM_LABEL);
					n->next = merge;
					bb->next = merge;
					bb = new_basic_block(ep, merge);
				}
			}
			break;
		}
			

		target = alloc_symbol(stmt->pos, SYM_LABEL);
		goto_bb = alloc_statement(stmt->pos, STMT_CONDFALSE);

		add_statement(&bb->stmts, goto_bb);
		last_bb = new_basic_block(ep, target);

		goto_bb->bb_conditional = cond;
		goto_bb->bb_target = target;

		bb = linearize_statement(ep, bb, stmt->if_true);
		bb->next = target;
		
		if (stmt->if_false) {
			struct symbol *else_target = alloc_symbol(stmt->pos, SYM_LABEL);
			struct basic_block *else_bb = new_basic_block(ep, else_target);

			else_bb = linearize_statement(ep, else_bb, stmt->if_false);
			goto_bb->bb_target = else_target;
			else_bb->next = target;
		}
				
		bb = last_bb;
		break;
	}

	case STMT_SWITCH: {
		struct symbol *sym;
		struct statement *switch_value;

		/* Create the "head node" */
		switch_value = alloc_statement(stmt->pos, STMT_MULTIVALUE);
		switch_value->expression = stmt->switch_expression;
		add_statement(&bb->stmts, switch_value);

		/* Create all the sub-jumps */
		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct statement *sw_bb = alloc_statement(case_stmt->pos, STMT_MULTIJMP);
			sw_bb->multi_from = case_stmt->case_expression;
			sw_bb->multi_to = case_stmt->case_to;
			sw_bb->multi_target = sym;
			add_statement(&bb->stmts, sw_bb);
		} END_FOR_EACH_PTR;

		/* Default fall-through case */
		bb->next = stmt->switch_break;

		/* And linearize the actual statement */
		bb = linearize_statement(ep, new_basic_block(ep, NULL), stmt->switch_statement);

		/* ..then tie it all together at the end.. */
		bb->next = stmt->switch_break;
		bb = new_basic_block(ep, stmt->switch_break);

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
		bb = linearize_statement(ep, bb, pre_statement);
		if (pre_condition) {
			if (pre_condition->type == EXPR_VALUE) {
				if (!pre_condition->value) {
					loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
					bb->next = loop_bottom;
					bb = new_basic_block(ep, loop_bottom);
				}
			} else {
				struct statement *pre_cond = alloc_statement(stmt->pos, STMT_CONDFALSE);
				loop_bottom = alloc_symbol(stmt->pos, SYM_LABEL);
				pre_cond->bb_conditional = pre_condition;
				pre_cond->bb_target = loop_bottom;
				add_statement(&bb->stmts, pre_cond);
			}
		}

		if (!post_condition || post_condition->type != EXPR_VALUE || post_condition->value) {
			struct basic_block *loop;

			loop_top = alloc_symbol(stmt->pos, SYM_LABEL);
			loop = new_basic_block(ep, loop_top);
			bb->next = loop_top;
			bb = loop;
		}

		bb = linearize_statement(ep, bb, statement);

		if (stmt->iterator_continue->used) {
			struct basic_block *cont = new_basic_block(ep, stmt->iterator_continue);
			bb->next = stmt->iterator_continue;
			bb = cont;
		}

		bb = linearize_statement(ep, bb, post_statement);

		if (!post_condition) {
			bb->next = loop_top;
		} else {
			if (post_condition->type != EXPR_VALUE || post_condition->value) {
				struct statement *post_cond = alloc_statement(stmt->pos, STMT_CONDTRUE);
				post_cond->bb_conditional = post_condition;
				post_cond->bb_target = loop_top;
				add_statement(&bb->stmts, post_cond);
			}
		}

		if (stmt->iterator_break->used) {
			struct basic_block *brk = new_basic_block(ep, stmt->iterator_break);
			bb->next = stmt->iterator_break;
			bb = brk;
		}
		break;
	}

	default:
		break;
	}
	return bb;
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
			struct basic_block *bb;

			ep->name = sym;
			bb = new_basic_block(ep, sym);
			concat_symbol_list(base_type->arguments, &ep->syms);
			linearize_statement(ep, bb, base_type->stmt);
			show_entry(ep);
		}
	}
}
