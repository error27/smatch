/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "linearize.h"

static int context_increase(struct basic_block *bb)
{
	int sum = 0;
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->opcode == OP_CONTEXT)
			sum += insn->increment;
	} END_FOR_EACH_PTR(insn);
	return sum;
}

static int imbalance(struct entrypoint *ep, struct basic_block *bb, int entry, int exit, const char *why)
{
	if (Wcontext) {
		struct symbol *sym = ep->name;
		warning(bb->pos, "context imbalance in '%s' - %s", show_ident(sym->ident), why);
	}
	return -1;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb, int entry, int exit);

static int check_children(struct entrypoint *ep, struct basic_block *bb, int entry, int exit)
{
	struct instruction *insn;
	struct basic_block *child;

	insn = last_instruction(bb->insns);
	if (!insn)
		return 0;
	if (insn->opcode == OP_RET)
		return entry != exit ? imbalance(ep, bb, entry, exit, "wrong count at exit") : 0;

	FOR_EACH_PTR(bb->children, child) {
		if (check_bb_context(ep, child, entry, exit))
			return -1;
	} END_FOR_EACH_PTR(child);
	return 0;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb, int entry, int exit)
{
	if (!bb)
		return 0;
	if (bb->context == entry)
		return 0;

	/* Now that's not good.. */
	if (bb->context >= 0)
		return imbalance(ep, bb, entry, bb->context, "different lock contexts for basic block");

	bb->context = entry;
	entry += context_increase(bb);
	if (entry < 0)
		return imbalance(ep, bb, entry, exit, "unexpected unlock");

	return check_children(ep, bb, entry, exit);
}

static void check_cast_instruction(struct instruction *insn)
{
	struct symbol *orig_type = insn->orig_type;
	if (orig_type) {
		int old = orig_type->bit_size;
		int new = insn->size;
		int oldsigned = (orig_type->ctype.modifiers & MOD_SIGNED) != 0;
		int newsigned = insn->opcode == OP_SCAST;

		if (new > old) {
			if (oldsigned == newsigned)
				return;
			if (newsigned)
				return;
			warning(insn->pos, "cast loses sign");
			return;
		}
		if (new < old) {
			warning(insn->pos, "cast drops bits");
			return;
		}
		if (oldsigned == newsigned) {
			warning(insn->pos, "cast wasn't removed");
			return;
		}
		warning(insn->pos, "cast changes sign");
	}
}

static void check_range_instruction(struct instruction *insn)
{
	warning(insn->pos, "value out of range");
}

static void check_one_instruction(struct instruction *insn)
{
	switch (insn->opcode) {
	case OP_CAST: case OP_SCAST:
		if (verbose)
			check_cast_instruction(insn);
		break;
	case OP_RANGE:
		check_range_instruction(insn);
		break;
	default:
		break;
	}
}

static void check_bb_instructions(struct basic_block *bb)
{
	struct instruction *insn;
	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		check_one_instruction(insn);
	} END_FOR_EACH_PTR(insn);
}

static void check_instructions(struct entrypoint *ep)
{
	struct basic_block *bb;
	FOR_EACH_PTR(ep->bbs, bb) {
		check_bb_instructions(bb);
	} END_FOR_EACH_PTR(bb);
}

static void check_context(struct entrypoint *ep)
{
	struct symbol *sym = ep->name;

	if (verbose && ep->entry->bb->needs) {
		pseudo_t pseudo;
		FOR_EACH_PTR(ep->entry->bb->needs, pseudo) {
			if (pseudo->type != PSEUDO_ARG)
				warning(sym->pos, "%s: possible uninitialized variable (%s)",
					show_ident(sym->ident), show_pseudo(pseudo));
		} END_FOR_EACH_PTR(pseudo);
	}

	check_instructions(ep);

	check_bb_context(ep, ep->entry->bb, sym->ctype.in_context, sym->ctype.out_context);
}

static void check_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			check_context(ep);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	// Expand, linearize and show it.
	check_symbols(sparse(argc, argv));
	return 0;
}
