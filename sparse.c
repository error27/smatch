/*
 * Example trivial client program that uses the sparse library
 * to tokenize, preprocess and parse a C file, and prints out
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

struct context_check {
	int val, val_false;
	char name[32];
};

DECLARE_ALLOCATOR(context_check);
DECLARE_PTR_LIST(context_check_list, struct context_check);
ALLOCATOR(context_check, "context check list");

static const char *unnamed_context = "<unnamed>";

static const char *context_name(struct context *context)
{
	if (context->context && context->context->symbol_name)
		return show_ident(context->context->symbol_name);
	return unnamed_context;
}

static void context_add(struct context_check_list **ccl, const char *name, int offs, int offs_false)
{
	struct context_check *check, *found = NULL;

	FOR_EACH_PTR(*ccl, check) {
		if (strcmp(name, check->name))
			continue;
		found = check;
		break;
	} END_FOR_EACH_PTR(check);

	if (!found) {
		found = __alloc_context_check(0);
		strncpy(found->name, name, sizeof(found->name));
		found->name[sizeof(found->name) - 1] = '\0';
		add_ptr_list(ccl, found);
	}
	found->val += offs;
	found->val_false += offs_false;
}

static int imbalance(struct entrypoint *ep, struct position pos, const char *name, const char *why)
{
	if (Wcontext) {
		struct symbol *sym = ep->name;
		if (strcmp(name, unnamed_context))
			warning(pos, "context imbalance in '%s' - %s (%s)", show_ident(sym->ident), why, name);
		else
			warning(pos, "context imbalance in '%s' - %s", show_ident(sym->ident), why);
	}
	return -1;
}

static int context_list_check(struct entrypoint *ep, struct position pos,
			      struct context_check_list *ccl_cur,
			      struct context_check_list *ccl_target)
{
	struct context_check *c1, *c2;
	int cur, tgt;

	/* make sure the loop below checks all */
	FOR_EACH_PTR(ccl_target, c1) {
		context_add(&ccl_cur, c1->name, 0, 0);
	} END_FOR_EACH_PTR(c1);

	FOR_EACH_PTR(ccl_cur, c1) {
		cur = c1->val;
		tgt = 0;

		FOR_EACH_PTR(ccl_target, c2) {
			if (strcmp(c2->name, c1->name))
				continue;
			tgt = c2->val;
			break;
		} END_FOR_EACH_PTR(c2);

		if (cur > tgt)
			return imbalance(ep, pos, c1->name, "wrong count at exit");
		else if (cur < tgt)
			return imbalance(ep, pos, c1->name, "unexpected unlock");
	} END_FOR_EACH_PTR(c1);

	return 0;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb,
			    struct context_check_list *ccl_in,
			    struct context_check_list *ccl_target,
			    int in_false)
{
	struct context_check_list *combined = NULL;
	struct context_check *c;
	struct instruction *insn;
	struct multijmp *mj;
	struct context *ctx;
	const char *name;
	int ok, val;

	/* recurse in once to catch bad loops */
	if (bb->context > 0)
		return 0;

	bb->context++;

	FOR_EACH_PTR(ccl_in, c) {
		if (in_false)
			context_add(&combined, c->name, c->val_false, c->val_false);
		else
			context_add(&combined, c->name, c->val, c->val);
	} END_FOR_EACH_PTR(c);

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		case OP_CALL:
			if (!insn->func || !insn->func->sym || insn->func->type != PSEUDO_SYM)
				break;
			FOR_EACH_PTR(insn->func->sym->ctype.contexts, ctx) {
				name = context_name(ctx);
				val = 0;

				FOR_EACH_PTR(combined, c) {
					if (strcmp(c->name, name) == 0) {
						val = c->val;
						break;
					}
				} END_FOR_EACH_PTR(c);

				if (ctx->exact)
					ok = ctx->in == val;
				else
					ok = ctx->in <= val;

				if (!ok) {
					const char *call = strdup(show_ident(insn->func->ident));
					warning(insn->pos, "context problem in '%s' - function '%s' expected different context",
						show_ident(ep->name->ident), call);
					free((void *)call);
					return -1;
				}
			} END_FOR_EACH_PTR (ctx);
			break;
		case OP_CONTEXT:
			val = 0;

			name = unnamed_context;
			if (insn->context_expr)
				name = show_ident(insn->context_expr->symbol_name);

			FOR_EACH_PTR(combined, c) {
				if (strcmp(c->name, name) == 0) {
					val = c->val;
					break;
				}
			} END_FOR_EACH_PTR(c);

			ok = insn->required <= val;

			if (!ok) {
				name = strdup(name);
				imbalance(ep, insn->pos, name, "__context__ statement expected different lock context");
				free((void *)name);
				return -1;
			}

			context_add(&combined, name, insn->increment, insn->inc_false);
			break;
		case OP_BR:
			if (insn->bb_true)
				if (check_bb_context(ep, insn->bb_true, combined, ccl_target, 0))
					return -1;
			if (insn->bb_false)
				if (check_bb_context(ep, insn->bb_false, combined, ccl_target, 1))
					return -1;
			break;
		case OP_SWITCH:
		case OP_COMPUTEDGOTO:
			FOR_EACH_PTR(insn->multijmp_list, mj) {
				if (check_bb_context(ep, mj->target, combined, ccl_target, 0))
					return -1;
			} END_FOR_EACH_PTR(mj);
			break;
		}
	} END_FOR_EACH_PTR(insn);

	insn = last_instruction(bb->insns);
	if (!insn)
		return 0;
	if (insn->opcode == OP_RET)
		return context_list_check(ep, insn->pos, combined, ccl_target);

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		}
	} END_FOR_EACH_PTR(insn);

	/* contents will be freed once we return out of recursion */
	free_ptr_list(&combined);

	return 0;
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

static void check_byte_count(struct instruction *insn, pseudo_t count)
{
	if (!count)
		return;
	if (count->type == PSEUDO_VAL) {
		long long val = count->value;
		if (val <= 0 || val > 100000)
			warning(insn->pos, "%s with byte count of %lld",
				show_ident(insn->func->sym->ident), val);
		return;
	}
	/* OK, we could try to do the range analysis here */
}

static pseudo_t argument(struct instruction *call, unsigned int argno)
{
	pseudo_t args[8];
	struct ptr_list *arg_list = (struct ptr_list *) call->arguments;

	argno--;
	if (linearize_ptr_list(arg_list, (void *)args, 8) > argno)
		return args[argno];
	return NULL;
}

static void check_memset(struct instruction *insn)
{
	check_byte_count(insn, argument(insn, 3));
}

#define check_memcpy check_memset
#define check_ctu check_memset
#define check_cfu check_memset

struct checkfn {
	struct ident *id;
	void (*check)(struct instruction *insn);
};

static void check_call_instruction(struct instruction *insn)
{
	pseudo_t fn = insn->func;
	struct ident *ident;
	static const struct checkfn check_fn[] = {
		{ &memset_ident, check_memset },
		{ &memcpy_ident, check_memcpy },
		{ &copy_to_user_ident, check_ctu },
		{ &copy_from_user_ident, check_cfu },
	};
	int i;

	if (fn->type != PSEUDO_SYM)
		return;
	ident = fn->sym->ident;
	if (!ident)
		return;
	for (i = 0; i < sizeof(check_fn)/sizeof(struct checkfn) ; i++) {
		if (check_fn[i].id != ident)
			continue;
		check_fn[i].check(insn);
		break;
	}
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
	case OP_CALL:
		check_call_instruction(insn);
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
	struct context *context;
	struct context_check_list *ccl_in = NULL, *ccl_target = NULL;

	if (Wuninitialized && verbose && ep->entry->bb->needs) {
		pseudo_t pseudo;
		FOR_EACH_PTR(ep->entry->bb->needs, pseudo) {
			if (pseudo->type != PSEUDO_ARG)
				warning(sym->pos, "%s: possible uninitialized variable (%s)",
					show_ident(sym->ident), show_pseudo(pseudo));
		} END_FOR_EACH_PTR(pseudo);
	}

	check_instructions(ep);

	FOR_EACH_PTR(sym->ctype.contexts, context) {
		const char *name = context_name(context);

		context_add(&ccl_in, name, context->in, context->in);
		context_add(&ccl_target, name, context->out, context->out_false);
		/* we don't currently check the body of trylock functions */
		if (context->out != context->out_false)
			return;
	} END_FOR_EACH_PTR(context);

	check_bb_context(ep, ep->entry->bb, ccl_in, ccl_target, 0);
	free_ptr_list(&ccl_in);
	free_ptr_list(&ccl_target);
	clear_context_check_alloc();
}

static void check_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep) {
			if (dbg_entry)
				show_entry(ep);

			check_context(ep);
		}
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;

	// Expand, linearize and show it.
	check_symbols(sparse_initialize(argc, argv, &filelist));
	FOR_EACH_PTR_NOTAG(filelist, file) {
		check_symbols(sparse(file));
	} END_FOR_EACH_PTR_NOTAG(file);
	return 0;
}
