/*
 * Flow - walk the linearized flowgraph, simplifying it as we
 * go along.
 *
 * Copyright (C) 2004 Linus Torvalds
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"

unsigned long bb_generation;

/*
 * Dammit, if we have a phi-node followed by a conditional
 * branch on that phi-node, we should damn well be able to
 * do something about the source. Maybe.
 */
static void rewrite_branch(struct basic_block *bb,
	struct basic_block **ptr,
	struct basic_block *old,
	struct basic_block *new)
{
	struct basic_block *tmp;

	if (*ptr != old)
		return;

	*ptr = new;
	FOR_EACH_PTR(new->parents, tmp) {
		if (tmp == old)
			*THIS_ADDRESS(tmp) = bb;
	} END_FOR_EACH_PTR(tmp);

	FOR_EACH_PTR(bb->children, tmp) {
		if (tmp == old)
			*THIS_ADDRESS(tmp) = new;
	} END_FOR_EACH_PTR(tmp);
}

/*
 * Return the known truth value of a pseudo, or -1 if
 * it's not known.
 */
static int pseudo_truth_value(pseudo_t pseudo)
{
	switch (pseudo->type) {
	case PSEUDO_VAL:
		return !!pseudo->value;

	case PSEUDO_REG: {
		struct instruction *insn = pseudo->def;
		if (insn->opcode == OP_SETVAL && insn->target == pseudo) {
			struct expression *expr = insn->val;

			/* A symbol address is always considered true.. */
			if (!expr)
				return 1;
			if (expr->type == EXPR_VALUE)
				return !!expr->value;
		}
	}
		/* Fall through */
	default:
		return -1;
	}
}


static void try_to_simplify_bb(struct entrypoint *ep, struct basic_block *bb,
	struct instruction *first, struct instruction *second)
{
	struct instruction *phi;

	FOR_EACH_PTR(first->phi_list, phi) {
		struct basic_block *source = phi->bb, *target;
		pseudo_t pseudo = phi->src1;
		struct instruction *br;
		int true;

		if (!pseudo || !source)
			continue;
		br = last_instruction(source->insns);
		if (!br)
			continue;
		if (br->opcode != OP_BR)
			continue;

		true = pseudo_truth_value(pseudo);
		if (true < 0)
			continue;
		target = true ? second->bb_true : second->bb_false;
		rewrite_branch(source, &br->bb_true, bb, target);
		rewrite_branch(source, &br->bb_false, bb, target);
	} END_FOR_EACH_PTR(phi);
}

static inline int linearize_insn_list(struct instruction_list *list, struct instruction **arr, int nr)
{
	return linearize_ptr_list((struct ptr_list *)list, (void **)arr, nr);
}

static void simplify_phi_nodes(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insns[2], *first, *second;

		if (linearize_insn_list(bb->insns, insns, 2) < 2)
			continue;

		first = insns[0];
		second = insns[1];

		if (first->opcode != OP_PHI)
			continue;
		if (second->opcode != OP_BR)
			continue;
		if (first->target != second->cond)
			continue;
		try_to_simplify_bb(ep, bb, first, second);
	} END_FOR_EACH_PTR(bb);
}

static inline void concat_user_list(struct pseudo_ptr_list *src, struct pseudo_ptr_list **dst)
{
	concat_ptr_list((struct ptr_list *)src, (struct ptr_list **)dst);
}

void convert_instruction_target(struct instruction *insn, pseudo_t src)
{
	pseudo_t target, *usep;

	/*
	 * Go through the "insn->users" list and replace them all..
	 */
	target = insn->target;
	FOR_EACH_PTR(target->users, usep) {
		*usep = src;
	} END_FOR_EACH_PTR(usep);
	concat_user_list(target->users, &src->users);
}

static void convert_load_insn(struct instruction *insn, pseudo_t src)
{
	convert_instruction_target(insn, src);
	/* Turn the load into a no-op */
	insn->opcode = OP_LNOP;
}

static int overlapping_memop(struct instruction *a, struct instruction *b)
{
	unsigned int a_start = (a->offset << 3) + a->type->bit_offset;
	unsigned int b_start = (b->offset << 3) + b->type->bit_offset;
	unsigned int a_size = a->type->bit_size;
	unsigned int b_size = b->type->bit_size;

	if (a_size + a_start <= b_start)
		return 0;
	if (b_size + b_start <= a_start)
		return 0;
	return 1;
}

static inline int same_memop(struct instruction *a, struct instruction *b)
{
	return	a->offset == b->offset &&
		a->type->bit_size == b->type->bit_size &&
		a->type->bit_offset == b->type->bit_offset;
}

/*
 * Return 1 if "one" dominates the access to 'pseudo'
 * in insn.
 *
 * Return 0 if it doesn't, and -1 if you don't know.
 */
static int dominates(pseudo_t pseudo, struct instruction *insn,
	struct instruction *one, int local)
{
	int opcode = one->opcode;

	if (opcode == OP_CALL)
		return local ? 0 : -1;
	if (opcode != OP_LOAD && opcode != OP_STORE)
		return 0;
	if (one->src != pseudo) {
		if (local)
			return 0;
		/* We don't think two explicitly different symbols ever alias */
		if (one->src->type == PSEUDO_SYM)
			return 0;
		/* We could try to do some alias analysis here */
		return -1;
	}
	if (!same_memop(insn, one)) {
		if (one->opcode == OP_LOAD)
			return 0;
		if (!overlapping_memop(insn, one))
			return 0;
		return -1;
	}
	return 1;
}

static int find_dominating_parents(pseudo_t pseudo, struct instruction *insn,
	struct basic_block *bb, unsigned long generation, struct pseudo_list **dominators,
	int local, int loads)
{
	struct basic_block *parent;

	if (bb_list_size(bb->parents) > 1)
		loads = 0;
	FOR_EACH_PTR(bb->parents, parent) {
		struct instruction *one;
		struct instruction *br;
		pseudo_t phi;

		FOR_EACH_PTR_REVERSE(parent->insns, one) {
			int dominance;
			if (one == insn)
				goto no_dominance;
			dominance = dominates(pseudo, insn, one, local);
			if (dominance < 0) {
				if (one->opcode == OP_LOAD)
					continue;
				return 0;
			}
			if (!dominance)
				continue;
			if (one->opcode == OP_LOAD && !loads)
				continue;
			goto found_dominator;
		} END_FOR_EACH_PTR_REVERSE(one);
no_dominance:
		if (parent->generation == generation)
			continue;
		parent->generation = generation;

		if (!find_dominating_parents(pseudo, insn, parent, generation, dominators, local, loads))
			return 0;
		continue;

found_dominator:
		br = delete_last_instruction(&parent->insns);
		phi = alloc_phi(parent, one->target);
		add_instruction(&parent->insns, br);
		add_pseudo(dominators, phi);
	} END_FOR_EACH_PTR(parent);
	return 1;
}		

/*
 * We should probably sort the phi list just to make it easier to compare
 * later for equality. 
 */
static void rewrite_load_instruction(struct instruction *insn, struct pseudo_list *dominators)
{
	pseudo_t new, phi;

	/*
	 * Check for somewhat common case of duplicate
	 * phi nodes.
	 */
	new = first_pseudo(dominators)->def->src1;
	FOR_EACH_PTR(dominators, phi) {
		if (new != phi->def->src1)
			goto complex_phi;
	} END_FOR_EACH_PTR(phi);

	/*
	 * All the same pseudo - mark the phi-nodes unused
	 * and convert the load into a LNOP and replace the
	 * pseudo.
	 */
	FOR_EACH_PTR(dominators, phi) {
		phi->def->bb = NULL;
	} END_FOR_EACH_PTR(phi);
	convert_load_insn(insn, new);
	return;

complex_phi:
	new = alloc_pseudo(insn);
	convert_load_insn(insn, new);

	/*
	 * FIXME! This is dubious. We should probably allocate a new
	 * instruction instead of re-using the OP_LOAD instruction.
	 * Re-use of the instruction makes the usage list suspect.
	 *
	 * It should be ok, because the only usage of the OP_LOAD
	 * is the symbol pseudo, and we should never follow that
	 * list _except_ for exactly the dominant instruction list
	 * generation (and then we always check the opcode).
	 */
	insn->opcode = OP_PHI;
	insn->target = new;
	insn->phi_list = dominators;
	new->def = insn;
}

static int find_dominating_stores(pseudo_t pseudo, struct instruction *insn,
	unsigned long generation, int local)
{
	struct basic_block *bb = insn->bb;
	struct instruction *one, *dom = NULL;
	struct pseudo_list *dominators;
	int partial;

	/* Unreachable load? Undo it */
	if (!bb) {
		insn->opcode = OP_LNOP;
		return 1;
	}

	partial = 0;
	FOR_EACH_PTR(bb->insns, one) {
		int dominance;
		if (one == insn)
			goto found;
		dominance = dominates(pseudo, insn, one, local);
		if (dominance < 0) {
			/* Ignore partial load dominators */
			if (one->opcode == OP_LOAD)
				continue;
			dom = NULL;
			partial = 1;
			continue;
		}
		if (!dominance)
			continue;
		dom = one;
		partial = 0;
	} END_FOR_EACH_PTR(one);
	/* Whaa? */
	warning(pseudo->sym->pos, "unable to find symbol read");
	return 0;
found:
	if (partial)
		return 0;

	if (dom) {
		convert_load_insn(insn, dom->target);
		return 1;
	}

	/* Ok, go find the parents */
	bb->generation = generation;

	dominators = NULL;
	if (!find_dominating_parents(pseudo, insn, bb, generation, &dominators, local, 1))
		return 0;

	/* This happens with initial assignments to structures etc.. */
	if (!dominators) {
		if (!local)
			return 0;
		convert_load_insn(insn, value_pseudo(0));
		return 1;
	}

	/*
	 * If we find just one dominating instruction, we
	 * can turn it into a direct thing. Otherwise we'll
	 * have to turn the load into a phi-node of the
	 * dominators.
	 */
	rewrite_load_instruction(insn, dominators);
	return 1;
}

/* Kill a pseudo that is dead on exit from the bb */
static void kill_dead_stores(pseudo_t pseudo, unsigned long generation, struct basic_block *bb, int local)
{
	struct instruction *insn;
	struct basic_block *parent;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR_REVERSE(bb->insns, insn) {
		int opcode = insn->opcode;

		if (opcode != OP_LOAD && opcode != OP_STORE) {
			if (local)
				continue;
			if (opcode == OP_CALL)
				return;
			continue;
		}
		if (insn->src == pseudo) {
			if (opcode == OP_LOAD)
				return;
			insn->opcode = OP_SNOP;
			continue;
		}
		if (local)
			continue;
		if (insn->src->type != PSEUDO_SYM)
			return;
	} END_FOR_EACH_PTR_REVERSE(insn);

	FOR_EACH_PTR(bb->parents, parent) {
		struct basic_block *child;
		FOR_EACH_PTR(parent->children, child) {
			if (child && child != bb)
				return;
		} END_FOR_EACH_PTR(child);
		kill_dead_stores(pseudo, generation, parent, local);
	} END_FOR_EACH_PTR(parent);
}

/*
 * This should see if the "insn" trivially dominates some previous store, and kill the
 * store if unnecessary.
 */
static void kill_dominated_stores(pseudo_t pseudo, struct instruction *insn, 
	unsigned long generation, struct basic_block *bb, int local, int found)
{
	struct instruction *one;
	struct basic_block *parent;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR_REVERSE(bb->insns, one) {
		int dominance;
		if (!found) {
			if (one != insn)
				continue;
			found = 1;
			continue;
		}
		dominance = dominates(pseudo, insn, one, local);
		if (!dominance)
			continue;
		if (dominance < 0)
			return;
		if (one->opcode == OP_LOAD)
			return;
		one->opcode = OP_SNOP;
	} END_FOR_EACH_PTR_REVERSE(one);

	if (!found) {
		warning(bb->pos, "Unable to find instruction");
		return;
	}

	FOR_EACH_PTR(bb->parents, parent) {
		struct basic_block *child;
		FOR_EACH_PTR(parent->children, child) {
			if (child && child != bb)
				return;
		} END_FOR_EACH_PTR(child);
		kill_dominated_stores(pseudo, insn, generation, parent, local, found);
	} END_FOR_EACH_PTR(parent);
}

static void simplify_one_symbol(struct entrypoint *ep, struct symbol *sym)
{
	pseudo_t pseudo, src, *pp;
	struct instruction *def;
	unsigned long mod;
	int all;

	/* Never used as a symbol? */
	pseudo = sym->pseudo;
	if (!pseudo)
		return;

	/* We don't do coverage analysis of volatiles.. */
	if (sym->ctype.modifiers & MOD_VOLATILE)
		return;

	/* ..and symbols with external visibility need more care */
	mod = sym->ctype.modifiers & (MOD_EXTERN | MOD_STATIC | MOD_ADDRESSABLE);
	if (mod)
		goto external_visibility;

	def = NULL;
	FOR_EACH_PTR(pseudo->users, pp) {
		/* We know that the symbol-pseudo use is the "src" in the instruction */
		struct instruction *insn = container(pp, struct instruction, src);

		switch (insn->opcode) {
		case OP_STORE:
			if (def)
				goto multi_def;
			def = insn;
			break;
		case OP_LOAD:
			break;
		default:
			warning(sym->pos, "symbol '%s' pseudo used in unexpected way", show_ident(sym->ident));
		}
		if (insn->offset)
			goto complex_def;
	} END_FOR_EACH_PTR(pp);

	/*
	 * Goodie, we have a single store (if even that) in the whole
	 * thing. Replace all loads with moves from the pseudo,
	 * replace the store with a def.
	 */
	src = VOID;
	if (def) {
		src = def->target;		

		/* Turn the store into a no-op */
		def->opcode = OP_SNOP;
	}
	FOR_EACH_PTR(pseudo->users, pp) {
		struct instruction *insn = container(pp, struct instruction, src);
		if (insn->opcode == OP_LOAD)
			convert_load_insn(insn, src);
	} END_FOR_EACH_PTR(pp);
	return;

multi_def:
complex_def:
external_visibility:
	all = 1;
	FOR_EACH_PTR_REVERSE(pseudo->users, pp) {
		struct instruction *insn = container(pp, struct instruction, src);
		if (insn->opcode == OP_LOAD)
			all &= find_dominating_stores(pseudo, insn, ++bb_generation, !mod);
	} END_FOR_EACH_PTR_REVERSE(pp);

	/* If we converted all the loads, remove the stores. They are dead */
	if (all && !mod) {
		FOR_EACH_PTR(pseudo->users, pp) {
			struct instruction *insn = container(pp, struct instruction, src);
			if (insn->opcode == OP_STORE)
				insn->opcode = OP_SNOP;
		} END_FOR_EACH_PTR(pp);
	} else {
		/*
		 * If we couldn't take the shortcut, see if we can at least kill some
		 * of them..
		 */
		FOR_EACH_PTR(pseudo->users, pp) {
			struct instruction *insn = container(pp, struct instruction, src);
			if (insn->opcode == OP_STORE)
				kill_dominated_stores(pseudo, insn, ++bb_generation, insn->bb, !mod, 0);
		} END_FOR_EACH_PTR(pp);

		if (!(mod & (MOD_EXTERN | MOD_STATIC))) {
			struct basic_block *bb;
			FOR_EACH_PTR(ep->bbs, bb) {
				if (!bb->children)
					kill_dead_stores(pseudo, ++bb_generation, bb, !mod);
			} END_FOR_EACH_PTR(bb);
		}
	}
			
	return;
}

void simplify_symbol_usage(struct entrypoint *ep)
{
	struct symbol *sym;

	FOR_EACH_PTR(ep->accesses, sym) {
		simplify_one_symbol(ep, sym);
		sym->pseudo = NULL;
	} END_FOR_EACH_PTR(sym);
}

static void mark_bb_reachable(struct basic_block *bb, unsigned long generation)
{
	struct basic_block *child;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR(bb->children, child) {
		mark_bb_reachable(child, generation);
	} END_FOR_EACH_PTR(child);
}

static void kill_bb(struct basic_block *bb)
{
	bb->insns = NULL;
	bb->children = NULL;
	bb->parents = NULL;
}

static void kill_unreachable_bbs(struct entrypoint *ep)
{
	struct basic_block *bb;
	unsigned long generation = ++bb_generation;

	mark_bb_reachable(ep->entry, generation);
	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;
		/* Mark it as being dead */
		kill_bb(bb);
	} END_FOR_EACH_PTR(bb);
}

static int rewrite_parent_branch(struct basic_block *bb, struct basic_block *old, struct basic_block *new)
{
	struct instruction *insn = last_instruction(bb->insns);

	if (!insn)
		return 0;
	switch (insn->opcode) {
	case OP_BR:
		rewrite_branch(bb, &insn->bb_true, old, new);
		rewrite_branch(bb, &insn->bb_false, old, new);
		if (insn->bb_true == insn->bb_false)
			insn->bb_false = NULL;
		return 1;
	case OP_SWITCH: {
		struct multijmp *jmp;
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			rewrite_branch(bb, &jmp->target, old, new);
		} END_FOR_EACH_PTR(jmp);
		return 1;
	}
	default:
		return 0;
	}
}

static struct basic_block * rewrite_branch_bb(struct basic_block *bb, struct instruction *br)
{
	struct basic_block *success;
	struct basic_block *parent;
	struct basic_block *target = br->bb_true;
	struct basic_block *false = br->bb_false;

	if (target && false) {
		pseudo_t cond = br->cond;
		if (cond->type != PSEUDO_VAL)
			return NULL;
		target = cond->value ? target : false;
	}

	target = target ? : false;
	success = target;
	FOR_EACH_PTR(bb->parents, parent) {
		if (!rewrite_parent_branch(parent, bb, target))
			success = NULL;
	} END_FOR_EACH_PTR(parent);
	return success;
}

/*
 * FIXME! This knows _way_ too much about list internals
 */
static void set_list(struct basic_block_list *p, struct basic_block *child)
{
	struct ptr_list *list = (void *)p;
	list->prev = list;
	list->next = list;
	list->nr = 1;
	list->list[0] = child;
}

static void simplify_one_switch(struct basic_block *bb,
	long long val,
	struct multijmp_list *list,
	struct instruction *p)
{
	struct multijmp *jmp;

	FOR_EACH_PTR(list, jmp) {
		/* Default case */
		if (jmp->begin > jmp->end)
			goto found;
		if (val >= jmp->begin && val <= jmp->end)
			goto found;
	} END_FOR_EACH_PTR(jmp);
	warning(bb->pos, "Impossible case statement");
	return;

found:
	p->opcode = OP_BR;
	p->cond = NULL;
	p->bb_false = NULL;
	p->bb_true = jmp->target;
	set_list(bb->children, jmp->target);
}

static void simplify_switch(struct entrypoint *ep)
{
	struct instruction *insn;

	FOR_EACH_PTR(ep->switches, insn) {
		pseudo_t pseudo = insn->target;
		if (pseudo->type == PSEUDO_VAL)
			simplify_one_switch(insn->bb, pseudo->value, insn->multijmp_list, insn);
	} END_FOR_EACH_PTR(insn);
}

void simplify_flow(struct entrypoint *ep)
{
	simplify_phi_nodes(ep);
	simplify_switch(ep);
	kill_unreachable_bbs(ep);
}

void pack_basic_blocks(struct entrypoint *ep)
{
	struct basic_block *bb;

	/* See if we can merge a bb into another one.. */
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *first;
		struct basic_block *parent, *child;

		if (!bb_reachable(bb))
			continue;

		/*
		 * Just a branch?
		 */
		FOR_EACH_PTR(bb->insns, first) {
			if (!first->bb)
				continue;
			switch (first->opcode) {
			case OP_NOP: case OP_LNOP: case OP_SNOP:
				continue;
			case OP_BR: {
				struct basic_block *replace;
				replace = rewrite_branch_bb(bb, first);
				if (replace) {
					if (ep->entry == bb)
						ep->entry = replace;
					kill_bb(bb);
					goto no_merge;
				}
			}
			/* fallthrough */
			default:
				goto out;
			}
		} END_FOR_EACH_PTR(first);

out:
		if (ep->entry == bb)
			continue;

		/*
		 * See if we only have one parent..
		 */
		if (bb_list_size(bb->parents) != 1)
			continue;
		parent = first_basic_block(bb->parents);
		if (parent == bb)
			continue;

		/*
		 * Goodie. See if the parent can merge..
		 */
		FOR_EACH_PTR(parent->children, child) {
			if (child != bb)
				goto no_merge;
		} END_FOR_EACH_PTR(child);

		parent->children = bb->children;
		FOR_EACH_PTR(bb->children, child) {
			struct basic_block *p;
			FOR_EACH_PTR(child->parents, p) {
				if (p != bb)
					continue;
				*THIS_ADDRESS(p) = parent;
			} END_FOR_EACH_PTR(p);
		} END_FOR_EACH_PTR(child);

		delete_last_instruction(&parent->insns);
		concat_instruction_list(bb->insns, &parent->insns);
		kill_bb(bb);

	no_merge:
		/* nothing to do */;
	} END_FOR_EACH_PTR(bb);
}


