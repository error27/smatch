/*
 * Copyright (C) 2004 Linus Torvalds
 */

///
// Flow simplification
// -------------------

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <assert.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"
#include "simplify.h"
#include "flow.h"
#include "target.h"
#include "flowgraph.h"

unsigned long bb_generation;

/*
 * Dammit, if we have a phi-node followed by a conditional
 * branch on that phi-node, we should damn well be able to
 * do something about the source. Maybe.
 */
static int rewrite_branch(struct basic_block *bb,
	struct basic_block **ptr,
	struct basic_block *old,
	struct basic_block *new)
{
	if (*ptr != old || new == old || !bb->ep)
		return 0;

	/* We might find new if-conversions or non-dominating CSEs */
	/* we may also create new dead cycles */
	repeat_phase |= REPEAT_CSE | REPEAT_CFG_CLEANUP;
	*ptr = new;
	replace_bb_in_list(&bb->children, old, new, 1);
	remove_bb_from_list(&old->parents, bb, 1);
	add_bb(&new->parents, bb);
	return 1;
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

		/* A symbol address is always considered true.. */
		if (insn->opcode == OP_SYMADDR && insn->target == pseudo)
			return 1;
	}
		/* Fall through */
	default:
		return -1;
	}
}

///
// check if the BB is empty or only contains phi-sources
static int bb_is_trivial(struct basic_block *bb)
{
	struct instruction *insn;
	int n = 0;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		case OP_TERMINATOR ... OP_TERMINATOR_END:
			return n ? -1 : 1;
		case OP_NOP:
		case OP_INLINED_CALL:
			continue;
		case OP_PHISOURCE:
			n++;
			continue;
		default:
			goto out;
		}
	} END_FOR_EACH_PTR(insn);

out:
	return 0;
}

/*
 * Does a basic block depend on the pseudos that "src" defines?
 */
static int bb_depends_on(struct basic_block *target, struct basic_block *src)
{
	pseudo_t pseudo;

	FOR_EACH_PTR(src->defines, pseudo) {
		if (pseudo_in_list(target->needs, pseudo))
			return 1;
	} END_FOR_EACH_PTR(pseudo);
	return 0;
}

/*
 * This really should be handled by bb_depends_on()
 * which efficiently check the dependence using the
 * defines - needs liveness info. Problem is that
 * there is no liveness done on OP_PHI & OP_PHISRC.
 *
 * This function add the missing dependency checks.
 */
static int bb_depends_on_phi(struct basic_block *target, struct basic_block *src)
{
	struct instruction *insn;
	FOR_EACH_PTR(src->insns, insn) {
		if (!insn->bb)
			continue;
		if (insn->opcode != OP_PHI)
			continue;
		if (pseudo_in_list(target->needs, insn->target))
			return 1;
	} END_FOR_EACH_PTR(insn);
	return 0;
}

///
// does the BB contains ignorable instructions but a final branch?
// :note: something could be done for phi-sources but ... we'll see.
static bool bb_is_forwarder(struct basic_block *bb)
{
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		case OP_NOP:
		case OP_INLINED_CALL:
			continue;
		case OP_CBR:
		case OP_BR:
			return true;
		default:
			goto out;
		}
	} END_FOR_EACH_PTR(insn);
out:
	return false;
}

///
// do jump threading in dominated BBs
// @dom: the BB which must dominate the modified BBs.
// @old: the old target BB
// @new: the new target BB
// @return: 0 if no chnages have been made, 1 otherwise.
//
// In all BB dominated by @dom, rewrite branches to @old into branches to @new
static int retarget_bb(struct basic_block *dom, struct basic_block *old, struct basic_block *new)
{
	struct basic_block *bb;
	int changed = 0;

	if (new == old)
		return 0;

restart:
	FOR_EACH_PTR(old->parents, bb) {
		struct instruction *last;
		struct multijmp *jmp;

		if (!domtree_dominates(dom, bb))
			continue;
		last = last_instruction(bb->insns);
		switch (last->opcode) {
		case OP_BR:
			changed |= rewrite_branch(bb, &last->bb_true,  old, new);
			break;
		case OP_CBR:
			changed |= rewrite_branch(bb, &last->bb_true,  old, new);
			changed |= rewrite_branch(bb, &last->bb_false, old, new);
			break;
		case OP_SWITCH:
		case OP_COMPUTEDGOTO:
			FOR_EACH_PTR(last->multijmp_list, jmp) {
				changed |= rewrite_branch(bb, &jmp->target, old, new);
			} END_FOR_EACH_PTR(jmp);
			break;
		default:
			continue;
		}

		// since rewrite_branch() will modify old->parents() the list
		// iteration won't work correctly. Several solution exist for
		// this but in this case the simplest is to restart the loop.
		goto restart;
	} END_FOR_EACH_PTR(bb);
	return changed;
}

static int simplify_cbr_cbr(struct instruction *insn)
{
	struct instruction *last;
	struct basic_block *bot = insn->bb;
	struct basic_block *top = bot->idom;
	int changed = 0;
	int trivial;

	if (!top)
		return 0;

	trivial = bb_is_trivial(bot);
	if (trivial == 0)
		return 0;
	if (trivial < 0)
		return 0;
	last = last_instruction(top->insns);
	if (last->opcode != OP_CBR || last->cond != insn->cond)
		return 0;

	changed |= retarget_bb(last->bb_true , bot, insn->bb_true);
	changed |= retarget_bb(last->bb_false, bot, insn->bb_false);
	return changed;
}

/*
 * When we reach here, we have:
 *  - a basic block that ends in a conditional branch and
 *    that has no side effects apart from the pseudos it
 *    may change.
 *  - the phi-node that the conditional branch depends on
 *  - full pseudo liveness information
 *
 * We need to check if any of the _sources_ of the phi-node
 * may be constant, and not actually need this block at all.
 */
static int try_to_simplify_bb(struct basic_block *bb, struct instruction *first, struct instruction *second)
{
	int changed = 0;
	pseudo_t phi;
	int bogus;

	/*
	 * This a due to improper dominance tracking during
	 * simplify_symbol_usage()/conversion to SSA form.
	 * No sane simplification can be done when we have this.
	 */
	bogus = bb_list_size(bb->parents) != pseudo_list_size(first->phi_list);

	FOR_EACH_PTR(first->phi_list, phi) {
		struct instruction *def = phi->def;
		struct basic_block *source, *target;
		pseudo_t pseudo;
		struct instruction *br;
		int cond;

		if (!def)
			continue;
		source = def->bb;
		pseudo = def->src1;
		if (!pseudo || !source)
			continue;
		br = last_instruction(source->insns);
		if (!br)
			continue;
		if (br->opcode != OP_CBR && br->opcode != OP_BR)
			continue;
		cond = pseudo_truth_value(pseudo);
		if (cond < 0)
			continue;
		target = cond ? second->bb_true : second->bb_false;
		if (bb_depends_on(target, bb))
			continue;
		if (bb_depends_on_phi(target, bb))
			continue;
		changed |= rewrite_branch(source, &br->bb_true, bb, target);
		changed |= rewrite_branch(source, &br->bb_false, bb, target);
		if (changed && !bogus)
			kill_use(THIS_ADDRESS(phi));
	} END_FOR_EACH_PTR(phi);
	return changed;
}

static int bb_has_side_effects(struct basic_block *bb)
{
	struct instruction *insn;
	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		case OP_CALL:
			/* FIXME! This should take "const" etc into account */
			return 1;

		case OP_LOAD:
			if (!insn->type)
				return 1;
			if (insn->is_volatile)
				return 1;
			continue;

		case OP_STORE:
		case OP_CONTEXT:
			return 1;

		case OP_ASM:
			/* FIXME! This should take "volatile" etc into account */
			return 1;

		default:
			continue;
		}
	} END_FOR_EACH_PTR(insn);
	return 0;
}

static int simplify_phi_branch(struct basic_block *bb, struct instruction *br)
{
	pseudo_t cond = br->cond;
	struct instruction *def;

	if (cond->type != PSEUDO_REG)
		return 0;
	def = cond->def;
	if (def->bb != bb || def->opcode != OP_PHI)
		return 0;
	if (bb_has_side_effects(bb))
		return 0;
	return try_to_simplify_bb(bb, def, br);
}

static int simplify_branch_branch(struct basic_block *bb, struct instruction *br,
	struct basic_block **target_p, int bb_true)
{
	struct basic_block *target = *target_p, *final;
	struct instruction *insn;
	int retval;

	if (target == bb)
		return 0;
	insn = last_instruction(target->insns);
	if (!insn || insn->opcode != OP_CBR || insn->cond != br->cond)
		return 0;
	/*
	 * Ahhah! We've found a branch to a branch on the same conditional!
	 * Now we just need to see if we can rewrite the branch..
	 */
	retval = 0;
	final = bb_true ? insn->bb_true : insn->bb_false;
	if (bb_has_side_effects(target))
		goto try_to_rewrite_target;
	if (bb_depends_on(final, target))
		goto try_to_rewrite_target;
	if (bb_depends_on_phi(final, target))
		return 0;
	return rewrite_branch(bb, target_p, target, final);

try_to_rewrite_target:
	/*
	 * If we're the only parent, at least we can rewrite the
	 * now-known second branch.
	 */
	if (bb_list_size(target->parents) != 1)
		return retval;
	insert_branch(target, insn, final);
	return 1;
}

static int simplify_one_branch(struct basic_block *bb, struct instruction *br)
{
	if (simplify_phi_branch(bb, br))
		return 1;
	if (simplify_cbr_cbr(br))
		return 1;
	return simplify_branch_branch(bb, br, &br->bb_true, 1) |
	       simplify_branch_branch(bb, br, &br->bb_false, 0);
}

static int simplify_branch_nodes(struct entrypoint *ep)
{
	int changed = 0;
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *br = last_instruction(bb->insns);

		if (!br || br->opcode != OP_CBR)
			continue;
		changed |= simplify_one_branch(bb, br);
	} END_FOR_EACH_PTR(bb);
	return changed;
}

/*
 * This is called late - when we have intra-bb liveness information..
 */
int simplify_flow(struct entrypoint *ep)
{
	return simplify_branch_nodes(ep);
}

static inline void concat_user_list(struct pseudo_user_list *src, struct pseudo_user_list **dst)
{
	copy_ptr_list((struct ptr_list **)dst, (struct ptr_list *)src);
}

void convert_instruction_target(struct instruction *insn, pseudo_t src)
{
	pseudo_t target;
	struct pseudo_user *pu;
	/*
	 * Go through the "insn->users" list and replace them all..
	 */
	target = insn->target;
	if (target == src)
		return;
	FOR_EACH_PTR(target->users, pu) {
		if (*pu->userp != VOID) {
			assert(*pu->userp == target);
			*pu->userp = src;
		}
	} END_FOR_EACH_PTR(pu);
	if (has_use_list(src))
		concat_user_list(target->users, &src->users);
	target->users = NULL;
}

static int overlapping_memop(struct instruction *a, struct instruction *b)
{
	unsigned int a_start = bytes_to_bits(a->offset);
	unsigned int b_start = bytes_to_bits(b->offset);
	unsigned int a_size = a->size;
	unsigned int b_size = b->size;

	if (a_size + a_start <= b_start)
		return 0;
	if (b_size + b_start <= a_start)
		return 0;
	return 1;
}

static inline int same_memop(struct instruction *a, struct instruction *b)
{
	return	a->offset == b->offset && a->size == b->size;
}

static inline int distinct_symbols(pseudo_t a, pseudo_t b)
{
	if (a->type != PSEUDO_SYM)
		return 0;
	if (b->type != PSEUDO_SYM)
		return 0;
	return a->sym != b->sym;
}

/*
 * Return 1 if "dom" dominates the access to "pseudo"
 * in "insn".
 *
 * Return 0 if it doesn't, and -1 if you don't know.
 */
int dominates(struct instruction *insn, struct instruction *dom, int local)
{
	switch (dom->opcode) {
	case OP_CALL: case OP_ENTRY:
		return local ? 0 : -1;
	case OP_LOAD: case OP_STORE:
		break;
	case OP_ASM:
		if (dom->clobber_memory)
			return -1;
		if (dom->output_memory)
			return -1;
		return 0;
	default:
		return 0;
	}

	if (dom->src != insn->src) {
		if (local)
			return 0;
		/* We don't think two explicitly different symbols ever alias */
		if (distinct_symbols(insn->src, dom->src))
			return 0;
		/* We could try to do some alias analysis here */
		return -1;
	}
	if (!same_memop(insn, dom)) {
		if (!overlapping_memop(insn, dom))
			return 0;
		return -1;
	}
	return 1;
}

/* Kill a pseudo that is dead on exit from the bb */
// The context is:
// * the variable is not global but may have its address used (local/non-local)
// * the stores are only needed by others functions which would do some
//   loads via the escaped address
// We start by the terminating BB (normal exit BB + no-return/unreachable)
// We walkup the BB' intruction backward
// * we're only concerned by loads, stores & calls
// * if we reach a call			-> we have to stop if var is non-local
// * if we reach a load of our var	-> we have to stop
// * if we reach a store of our var	-> we can kill it, it's dead
// * we can ignore other stores & loads if the var is local
// * if we reach another store or load done via non-symbol access
//   (so done via some address calculation) -> we have to stop
// If we reach the top of the BB we can recurse into the parents BBs.
static void kill_dead_stores_bb(pseudo_t pseudo, unsigned long generation, struct basic_block *bb, int local)
{
	struct instruction *insn;
	struct basic_block *parent;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR_REVERSE(bb->insns, insn) {
		if (!insn->bb)
			continue;
		switch (insn->opcode) {
		case OP_LOAD:
			if (insn->src == pseudo)
				return;
			break;
		case OP_STORE:
			if (insn->src == pseudo) {
				kill_instruction_force(insn);
				continue;
			}
			break;
		case OP_CALL:
			if (!local)
				return;
		default:
			continue;
		}
		if (!local && insn->src->type != PSEUDO_SYM)
			return;
	} END_FOR_EACH_PTR_REVERSE(insn);

	FOR_EACH_PTR(bb->parents, parent) {
		if (bb_list_size(parent->children) > 1)
			continue;
		kill_dead_stores_bb(pseudo, generation, parent, local);
	} END_FOR_EACH_PTR(parent);
}

void check_access(struct instruction *insn)
{
	pseudo_t pseudo = insn->src;

	if (insn->bb && pseudo->type == PSEUDO_SYM) {
		int offset = insn->offset, bit = bytes_to_bits(offset) + insn->size;
		struct symbol *sym = pseudo->sym;

		if (sym->bit_size > 0 && (offset < 0 || bit > sym->bit_size)) {
			if (insn->tainted)
				return;
			warning(insn->pos, "invalid access %s '%s' (%d %d)",
				offset < 0 ? "below" : "past the end of",
				show_ident(sym->ident), offset,
				bits_to_bytes(sym->bit_size));
			insn->tainted = 1;
		}
	}
}

static struct pseudo_user *first_user(pseudo_t p)
{
	struct pseudo_user *pu;
	FOR_EACH_PTR(p->users, pu) {
		if (!pu)
			continue;
		return pu;
	} END_FOR_EACH_PTR(pu);
	return NULL;
}

void kill_dead_stores(struct entrypoint *ep, pseudo_t addr, int local)
{
	unsigned long generation;
	struct basic_block *bb;

	switch (pseudo_user_list_size(addr->users)) {
	case 0:
		return;
	case 1:
		if (local) {
			struct pseudo_user *pu = first_user(addr);
			struct instruction *insn = pu->insn;
			if (insn->opcode == OP_STORE) {
				kill_instruction_force(insn);
				return;
			}
		}
	default:
		break;
	}

	generation = ++bb_generation;
	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->children)
			continue;
		kill_dead_stores_bb(addr, generation, bb, local);
	} END_FOR_EACH_PTR(bb);
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

static void kill_defs(struct instruction *insn)
{
	pseudo_t target = insn->target;

	if (!has_use_list(target))
		return;
	if (target->def != insn)
		return;

	convert_instruction_target(insn, VOID);
}

void kill_bb(struct basic_block *bb)
{
	struct instruction *insn;
	struct basic_block *child, *parent;

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		kill_instruction_force(insn);
		kill_defs(insn);
		/*
		 * We kill unreachable instructions even if they
		 * otherwise aren't "killable" (e.g. volatile loads)
		 */
	} END_FOR_EACH_PTR(insn);
	bb->insns = NULL;

	FOR_EACH_PTR(bb->children, child) {
		remove_bb_from_list(&child->parents, bb, 0);
	} END_FOR_EACH_PTR(child);
	bb->children = NULL;

	FOR_EACH_PTR(bb->parents, parent) {
		remove_bb_from_list(&parent->children, bb, 0);
	} END_FOR_EACH_PTR(parent);
	bb->parents = NULL;
}

void kill_unreachable_bbs(struct entrypoint *ep)
{
	struct basic_block *bb;
	unsigned long generation = ++bb_generation;

	mark_bb_reachable(ep->entry->bb, generation);
	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->generation == generation)
			continue;
		/* Mark it as being dead */
		kill_bb(bb);
		bb->ep = NULL;
		DELETE_CURRENT_PTR(bb);
	} END_FOR_EACH_PTR(bb);
	PACK_PTR_LIST(&ep->bbs);
}

static int rewrite_parent_branch(struct basic_block *bb, struct basic_block *old, struct basic_block *new)
{
	int changed = 0;
	struct instruction *insn = last_instruction(bb->insns);

	if (!insn)
		return 0;

	/* Infinite loops: let's not "optimize" them.. */
	if (old == new)
		return 0;

	switch (insn->opcode) {
	case OP_CBR:
		changed |= rewrite_branch(bb, &insn->bb_false, old, new);
		/* fall through */
	case OP_BR:
		changed |= rewrite_branch(bb, &insn->bb_true, old, new);
		assert(changed);
		return changed;
	case OP_SWITCH: {
		struct multijmp *jmp;
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			changed |= rewrite_branch(bb, &jmp->target, old, new);
		} END_FOR_EACH_PTR(jmp);
		assert(changed);
		return changed;
	}
	default:
		return 0;
	}
}

static struct basic_block * rewrite_branch_bb(struct basic_block *bb, struct instruction *br)
{
	struct basic_block *parent;
	struct basic_block *target = br->bb_true;

	if (br->opcode == OP_CBR) {
		pseudo_t cond = br->cond;
		if (cond->type != PSEUDO_VAL)
			return NULL;
		target = cond->value ? target : br->bb_false;
	}

	/*
	 * We can't do FOR_EACH_PTR() here, because the parent list
	 * may change when we rewrite the parent.
	 */
	while ((parent = first_basic_block(bb->parents)) != NULL) {
		if (!rewrite_parent_branch(parent, bb, target))
			return NULL;
	}
	return target;
}

static void vrfy_bb_in_list(struct basic_block *bb, struct basic_block_list *list)
{
	if (bb) {
		struct basic_block *tmp;
		int no_bb_in_list = 0;

		FOR_EACH_PTR(list, tmp) {
			if (bb == tmp)
				return;
		} END_FOR_EACH_PTR(tmp);
		assert(no_bb_in_list);
	}
}

static void vrfy_parents(struct basic_block *bb)
{
	struct basic_block *tmp;
	FOR_EACH_PTR(bb->parents, tmp) {
		vrfy_bb_in_list(bb, tmp->children);
	} END_FOR_EACH_PTR(tmp);
}

static void vrfy_children(struct basic_block *bb)
{
	struct basic_block *tmp;
	struct instruction *br = last_instruction(bb->insns);

	if (!br) {
		assert(!bb->children);
		return;
	}
	switch (br->opcode) {
		struct multijmp *jmp;
	case OP_CBR:
		vrfy_bb_in_list(br->bb_false, bb->children);
		/* fall through */
	case OP_BR:
		vrfy_bb_in_list(br->bb_true, bb->children);
		break;
	case OP_SWITCH:
	case OP_COMPUTEDGOTO:
		FOR_EACH_PTR(br->multijmp_list, jmp) {
			vrfy_bb_in_list(jmp->target, bb->children);
		} END_FOR_EACH_PTR(jmp);
		break;
	default:
		break;
	}
		
	FOR_EACH_PTR(bb->children, tmp) {
		vrfy_bb_in_list(bb, tmp->parents);
	} END_FOR_EACH_PTR(tmp);
}

static void vrfy_bb_flow(struct basic_block *bb)
{
	vrfy_children(bb);
	vrfy_parents(bb);
}

void vrfy_flow(struct entrypoint *ep)
{
	struct basic_block *bb;
	struct basic_block *entry = ep->entry->bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb == entry)
			entry = NULL;
		vrfy_bb_flow(bb);
	} END_FOR_EACH_PTR(bb);
	assert(!entry);
}

static int retarget_parents(struct basic_block *bb, struct basic_block *target)
{
	struct basic_block *parent;

	/*
	 * We can't do FOR_EACH_PTR() here, because the parent list
	 * may change when we rewrite the parent.
	 */
	while ((parent = first_basic_block(bb->parents))) {
		if (!rewrite_parent_branch(parent, bb, target))
			return 0;
	}
	kill_bb(bb);
	return REPEAT_CFG_CLEANUP;
}

static void remove_merging_phisrc(struct basic_block *top, struct instruction *insn)
{
	struct instruction *user = insn->phi_node;
	pseudo_t phi;

	FOR_EACH_PTR(user->phi_list, phi) {
		struct instruction *phisrc;

		if (phi == VOID)
			continue;
		phisrc = phi->def;
		if (phisrc->bb != top)
			continue;
		REPLACE_CURRENT_PTR(phi, VOID);
		kill_instruction(phisrc);
	} END_FOR_EACH_PTR(phi);
}

static void remove_merging_phi(struct basic_block *top, struct instruction *insn)
{
	pseudo_t phi;

	FOR_EACH_PTR(insn->phi_list, phi) {
		struct instruction *def;

		if (phi == VOID)
			continue;

		def = phi->def;
		if (def->bb != top)
			continue;

		convert_instruction_target(insn, def->src);
		kill_instruction(def);
		kill_instruction(insn);
	} END_FOR_EACH_PTR(phi);
}

///
// merge two BBs
// @top: the first BB to be merged
// @bot: the second BB to be merged
static int merge_bb(struct basic_block *top, struct basic_block *bot)
{
	struct instruction *insn;
	struct basic_block *bb;

	if (top == bot)
		return 0;

	top->children = bot->children;
	bot->children = NULL;
	bot->parents = NULL;

	FOR_EACH_PTR(top->children, bb) {
		replace_bb_in_list(&bb->parents, bot, top, 1);
	} END_FOR_EACH_PTR(bb);

	kill_instruction(delete_last_instruction(&top->insns));
	FOR_EACH_PTR(bot->insns, insn) {
		if (!insn->bb)
			continue;
		assert(insn->bb == bot);
		switch (insn->opcode) {
		case OP_PHI:
			remove_merging_phi(top, insn);
			continue;
		case OP_PHISOURCE:
			remove_merging_phisrc(top, insn);
			break;
		}
		insn->bb = top;
		add_instruction(&top->insns, insn);
	} END_FOR_EACH_PTR(insn);
	bot->insns = NULL;
	bot->ep = NULL;
	return REPEAT_CFG_CLEANUP;
}

///
// early simplification of the CFG
// Three things are done here:
//    # inactive BB are removed
//    # branches to a 'forwarder' BB are redirected to the forwardee.
//    # merge single-child/single-parent BBs.
int simplify_cfg_early(struct entrypoint *ep)
{
	struct basic_block *bb;
	int changed = 0;

	FOR_EACH_PTR_REVERSE(ep->bbs, bb) {
		struct instruction *insn;
		struct basic_block *tgt;

		if (!bb->ep) {
			DELETE_CURRENT_PTR(bb);
			changed = REPEAT_CFG_CLEANUP;
			continue;
		}

		insn = last_instruction(bb->insns);
		if (!insn)
			continue;
		switch (insn->opcode) {
		case OP_BR:
			tgt = insn->bb_true;
			if (bb_is_forwarder(bb))
				changed |= retarget_parents(bb, tgt);
			else if (bb_list_size(tgt->parents) == 1)
				changed |= merge_bb(bb, tgt);
			break;
		}
	} END_FOR_EACH_PTR_REVERSE(bb);
	return changed;
}

void pack_basic_blocks(struct entrypoint *ep)
{
	struct basic_block *bb;

	/* See if we can merge a bb into another one.. */
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *first;
		struct basic_block *parent, *child, *last;

		if (!bb_reachable(bb))
			continue;

		/*
		 * Just a branch?
		 */
		FOR_EACH_PTR(bb->insns, first) {
			if (!first->bb)
				continue;
			switch (first->opcode) {
			case OP_NOP:
			case OP_INLINED_CALL:
				continue;
			case OP_CBR:
			case OP_BR: {
				struct basic_block *replace;
				replace = rewrite_branch_bb(bb, first);
				if (replace) {
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
		/*
		 * See if we only have one parent..
		 */
		last = NULL;
		FOR_EACH_PTR(bb->parents, parent) {
			if (last) {
				if (last != parent)
					goto no_merge;
				continue;
			}
			last = parent;
		} END_FOR_EACH_PTR(parent);

		parent = last;
		if (!parent || parent == bb)
			continue;

		/*
		 * Goodie. See if the parent can merge..
		 */
		FOR_EACH_PTR(parent->children, child) {
			if (child != bb)
				goto no_merge;
		} END_FOR_EACH_PTR(child);

		repeat_phase |= merge_bb(parent, bb);

	no_merge:
		/* nothing to do */;
	} END_FOR_EACH_PTR(bb);
}


