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

	printf("bb: %p\n", bb);
	FOR_EACH_PTR(bb->stmts, stmt) {
		show_statement(stmt);
	} END_FOR_EACH_PTR;

	if (bb->next) {
		printf("\tgoto .L%p\n", bb->next->bb_target);
	} else {
		printf("\tdefault return\n");
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

static struct basic_block * new_basic_block(struct basic_block_list **bbs)
{
	struct basic_block *bb = alloc_basic_block();
	add_bb(bbs, bb);
	return bb;
}

static struct basic_block * linearize_statement(struct symbol_list **syms,
	struct basic_block_list **bbs,
	struct basic_block *bb,
	struct statement *stmt)
{
	if (!stmt)
		return bb;

	switch (stmt->type) {
	case STMT_NONE:
		return bb;

	case STMT_EXPRESSION:
		add_statement(&bb->stmts, stmt);
		break;

	case STMT_RETURN:
		add_statement(&bb->stmts, stmt);
		bb = new_basic_block(bbs);
		break;

	case STMT_LABEL: {
		struct basic_block *new_bb = new_basic_block(bbs);
		struct symbol *sym = stmt->label_identifier;

		bb->next = sym;
		sym->bb_target = new_bb;

		bb = linearize_statement(syms, bbs, new_bb, stmt->label_statement);
		break;
	}

	case STMT_GOTO: {
		struct basic_block *new_bb = new_basic_block(bbs);
		struct symbol *sym = stmt->goto_label;

		bb->next = sym;
		bb = new_bb;
		break;
	}

	case STMT_COMPOUND: {
		struct statement *s;
		concat_symbol_list(stmt->syms, syms);
		FOR_EACH_PTR(stmt->stmts, s) {
			bb = linearize_statement(syms, bbs, bb, s);
		} END_FOR_EACH_PTR;
		break;
	}

	/*
	 * This could take 'likely/unlikely' into account, and
	 * switch the arms around appropriately..
	 */
	case STMT_IF: {
		struct symbol *target = alloc_symbol(stmt->pos, SYM_LABEL);
		struct statement *goto_bb = alloc_statement(stmt->pos, STMT_GOTO_BB);
		struct basic_block *last_bb;

		add_statement(&bb->stmts, goto_bb);
		last_bb = new_basic_block(bbs);

		goto_bb->bb_conditional = stmt->if_conditional;
		goto_bb->bb_target = last_bb;

		bb = linearize_statement(syms, bbs, bb, stmt->if_true);
		bb->next = target;
		target->bb_target = last_bb;
		
		if (stmt->if_false) {
			struct symbol *else_target = alloc_symbol(stmt->pos, SYM_LABEL);
			struct basic_block *else_bb = new_basic_block(bbs);

			else_target->bb_target = else_bb;
			else_bb = linearize_statement(syms, bbs, else_bb, stmt->if_false);
			goto_bb->bb_target = else_bb;
			else_bb->next = target;
		}
				
		bb = last_bb;
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
			bb = new_basic_block(&ep->bbs);
			concat_symbol_list(base_type->arguments, &ep->syms);
			linearize_statement(&ep->syms, &ep->bbs, bb, base_type->stmt);
			show_entry(ep);
		}
	}
}
