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
DECLARE_PTR_LIST(context_list_list, struct context_check_list);
ALLOCATOR(context_check, "context check list");

static const char *unnamed_context = "<unnamed>";

static const char *context_name(struct context *context)
{
	if (context->context && context->context->symbol_name)
		return show_ident(context->context->symbol_name);
	return unnamed_context;
}

static void context_add(struct context_check_list **ccl, const char *name,
			int offs, int offs_false)
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

static int context_list_has(struct context_check_list *ccl,
			    struct context_check *c)
{
	struct context_check *check;

	FOR_EACH_PTR(ccl, check) {
		if (strcmp(c->name, check->name))
			continue;
		return check->val == c->val &&
		       check->val_false == c->val_false;
	} END_FOR_EACH_PTR(check);

	/* not found is equal to 0 */
	return c->val == 0 && c->val_false == 0;
}

static int context_lists_equal(struct context_check_list *ccl1,
			       struct context_check_list *ccl2)
{
	struct context_check *check;

	/* can be optimised... */

	FOR_EACH_PTR(ccl1, check) {
		if (!context_list_has(ccl2, check))
			return 0;
	} END_FOR_EACH_PTR(check);

	FOR_EACH_PTR(ccl2, check) {
		if (!context_list_has(ccl1, check))
			return 0;
	} END_FOR_EACH_PTR(check);

	return 1;
}

static struct context_check_list *checked_copy(struct context_check_list *ccl)
{
	struct context_check_list *result = NULL;
	struct context_check *c;

	FOR_EACH_PTR(ccl, c) {
		context_add(&result, c->name, c->val_false, c->val_false);
	} END_FOR_EACH_PTR(c);

	return result;
}

#define IMBALANCE_IN "context imbalance in '%s': "
#define DEFAULT_CONTEXT_DESCR "   default context: "

static void get_context_string(char **buf, const char **name)
{
	if (strcmp(*name, unnamed_context)) {
		*buf = malloc(strlen(*name) + 16);
		sprintf(*buf, "   context '%s': ", *name);
		*name = *buf;
	} else {
		*name = DEFAULT_CONTEXT_DESCR;
		*buf = NULL;
	}
}

static int context_list_check(struct entrypoint *ep, struct position pos,
			      struct context_check_list *ccl_cur,
			      struct context_check_list *ccl_target)
{
	struct context_check *c1, *c2;
	int cur, tgt;
	const char *name;
	char *buf;

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

		if (cur == tgt || !Wcontext)
			continue;

		if (cur > tgt)
			warning(pos, IMBALANCE_IN "wrong count at exit",
				show_ident(ep->name->ident));
		else if (cur < tgt)
			warning(pos, IMBALANCE_IN "unexpected unlock",
				show_ident(ep->name->ident));

		name = c1->name;
		get_context_string(&buf, &name);

		info(pos, "%swanted %d, got %d",
		     name, tgt, cur);

		free(buf);

		return -1;
	} END_FOR_EACH_PTR(c1);

	return 0;
}

static int handle_call(struct entrypoint *ep, struct basic_block *bb,
		       struct instruction *insn,
		       struct context_check_list *combined)
{
	struct context *ctx;
	struct context_check *c;
	const char *name, *call, *cmp;
	char *buf;
	int val, ok;

	if (!insn->func || !insn->func->sym ||
	    insn->func->type != PSEUDO_SYM)
		return 0;

	/*
	 * Check all contexts the function wants.
	 */
	FOR_EACH_PTR(insn->func->sym->ctype.contexts, ctx) {
		name = context_name(ctx);
		val = 0;

		FOR_EACH_PTR(combined, c) {
			if (strcmp(c->name, name) == 0) {
				val = c->val;
				break;
			}
		} END_FOR_EACH_PTR(c);

		if (ctx->exact) {
			ok = ctx->in == val;
			cmp = "";
		} else {
			ok = ctx->in <= val;
			cmp = ">= ";
		}

		if (!ok && Wcontext) {
			get_context_string(&buf, &name);
			call = strdup(show_ident(insn->func->ident));

			warning(insn->pos, "context problem in '%s': "
				"'%s' expected different context",
				show_ident(ep->name->ident), call);

			info(insn->pos, "%swanted %s%d, got %d",
			     name, cmp, ctx->in, val);

			free((void *)call);
			free(buf);

			return -1;
		}
	} END_FOR_EACH_PTR (ctx);

	return 0;
}

static int handle_context(struct entrypoint *ep, struct basic_block *bb,
			  struct instruction *insn,
			  struct context_check_list **combined)
{
	struct context_check *c;
	const char *name;
	char *buf;
	int val, ok;

	val = 0;

	name = unnamed_context;
	if (insn->context_expr)
		name = show_ident(insn->context_expr->symbol_name);

	FOR_EACH_PTR(*combined, c) {
		if (strcmp(c->name, name) == 0) {
			val = c->val;
			break;
		}
	} END_FOR_EACH_PTR(c);

	ok = insn->required <= val;

	if (!ok && Wcontext) {
		get_context_string(&buf, &name);

		warning(insn->pos,
			IMBALANCE_IN
			"__context__ statement expected different context",
			show_ident(ep->name->ident));

		info(insn->pos, "%swanted >= %d, got %d",
		     name, insn->required, val);

		free(buf);
		return -1;
	}

	context_add(combined, name, insn->increment, insn->inc_false);

	return 0;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb,
			    struct context_check_list *ccl_in,
			    struct context_check_list *ccl_target,
			    int in_false)
{
	struct context_check_list *combined = NULL, *done;
	struct context_check *c;
	struct instruction *insn;
	struct multijmp *mj;
	int err = -1;

	/*
	 * Recurse in once to catch bad loops.
	 */
	if (bb->context_check_recursion > 1)
		return 0;
	bb->context_check_recursion++;

	/*
	 * Abort if we have already checked this block out of the same context.
	 */
	FOR_EACH_PTR(bb->checked_contexts, done) {
		if (context_lists_equal(done, ccl_in))
			return 0;
	} END_FOR_EACH_PTR(done);

	/*
	 * We're starting with a completely new local list of contexts, so
	 * initialise it according to what we got from the parent block.
	 * That may use either the 'false' or the 'true' part of the context
	 * for the conditional_context() attribute.
	 */
	FOR_EACH_PTR(ccl_in, c) {
		if (in_false)
			context_add(&combined, c->name, c->val_false, c->val_false);
		else
			context_add(&combined, c->name, c->val, c->val);
	} END_FOR_EACH_PTR(c);

	/* Add the new context to the list of already-checked contexts */
	done = checked_copy(combined);
	add_ptr_list(&bb->checked_contexts, done);

	/*
	 * Now walk the instructions for this block, recursing into any
	 * instructions that have children. We need to have the right
	 * order so we cannot iterate bb->children instead.
	 */
	FOR_EACH_PTR(bb->insns, insn) {
		switch (insn->opcode) {
		case OP_INLINED_CALL:
		case OP_CALL:
			if (handle_call(ep, bb, insn, combined))
				goto out;
			break;
		case OP_CONTEXT:
			if (handle_context(ep, bb, insn, &combined))
				goto out;
			break;
		case OP_BR:
			if (insn->bb_true)
				if (check_bb_context(ep, insn->bb_true,
						     combined, ccl_target, 0))
					goto out;
			if (insn->bb_false)
				if (check_bb_context(ep, insn->bb_false,
						     combined, ccl_target, 1))
					goto out;
			break;
		case OP_SWITCH:
		case OP_COMPUTEDGOTO:
			FOR_EACH_PTR(insn->multijmp_list, mj) {
				if (check_bb_context(ep, mj->target,
					             combined, ccl_target, 0))
					goto out;
			} END_FOR_EACH_PTR(mj);
			break;
		}
	} END_FOR_EACH_PTR(insn);

	insn = last_instruction(bb->insns);
	if (!insn)
		goto out_good;

	if (insn->opcode == OP_RET) {
		err = context_list_check(ep, insn->pos, combined, ccl_target);
		goto out;
	}

 out_good:
	err = 0;
 out:
	/* contents will be freed once we return out of recursion */
	free_ptr_list(&combined);
	bb->context_check_recursion--;
	return err;
}

static void free_bb_context_lists(struct basic_block *bb)
{
	struct context_check_list *done;
	struct instruction *insn;
	struct multijmp *mj;

	if (!bb->checked_contexts)
		return;

	FOR_EACH_PTR(bb->checked_contexts, done) {
		free_ptr_list(&done);
	} END_FOR_EACH_PTR(done);

	free_ptr_list(&bb->checked_contexts);

	FOR_EACH_PTR(bb->insns, insn) {
		switch (insn->opcode) {
		case OP_BR:
			if (insn->bb_true)
				free_bb_context_lists(insn->bb_true);
			if (insn->bb_false)
				free_bb_context_lists(insn->bb_false);
			break;
		case OP_SWITCH:
		case OP_COMPUTEDGOTO:
			FOR_EACH_PTR(insn->multijmp_list, mj) {
				free_bb_context_lists(mj->target);
			} END_FOR_EACH_PTR(mj);
			break;
		}
	} END_FOR_EACH_PTR(insn);
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
	free_bb_context_lists(ep->entry->bb);
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
