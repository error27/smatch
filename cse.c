/*
 * CSE - walk the linearized instruction flow, and
 * see if we can simplify it and apply CSE on it.
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

#define INSN_HASH_SIZE 65536
static struct instruction_list *insn_hash_table[INSN_HASH_SIZE];

#define hashval(x) ((unsigned long)(x))

static int cse_repeat;

static int phi_compare(pseudo_t phi1, pseudo_t phi2)
{
	const struct instruction *def1 = phi1->def;
	const struct instruction *def2 = phi2->def;

	if (def1->src1 != def2->src1)
		return def1->src1 < def2->src1 ? -1 : 1;
	if (def1->bb != def2->bb)
		return def1->bb < def2->bb ? -1 : 1;
	return 0;
}

/* Find the trivial parent for a phi-source */
static struct basic_block *phi_parent(struct basic_block *source, pseudo_t pseudo)
{
	/* Can't go upwards if the pseudo is defined in the bb it came from.. */
	if (pseudo->type == PSEUDO_REG) {
		struct instruction *def = pseudo->def;
		if (def->bb == source)
			return source;
	}
	if (bb_list_size(source->children) != 1 || bb_list_size(source->parents) != 1)
		return source;
	return first_basic_block(source->parents);
}

static void clear_phi(struct instruction *insn)
{
	pseudo_t phi;

	insn->bb = NULL;
	FOR_EACH_PTR(insn->phi_list, phi) {
		struct instruction *def = phi->def;
		def->bb = NULL;
	} END_FOR_EACH_PTR(phi);
}

static void if_convert_phi(struct instruction *insn)
{
	pseudo_t array[3];
	struct basic_block *parents[3];
	struct basic_block *bb, *bb1, *bb2, *source;
	struct instruction *br;
	pseudo_t p1, p2;

	bb = insn->bb;
	if (linearize_ptr_list((struct ptr_list *)insn->phi_list, (void **)array, 3) != 2)
		return;
	if (linearize_ptr_list((struct ptr_list *)bb->parents, (void **)parents, 3) != 2)
		return;
	p1 = array[0]->def->src1;
	bb1 = array[0]->def->bb;
	p2 = array[1]->def->src1;
	bb2 = array[1]->def->bb;

	/* Only try the simple "direct parents" case */
	if ((bb1 != parents[0] || bb2 != parents[1]) &&
	    (bb1 != parents[1] || bb2 != parents[0]))
		return;

	/*
	 * See if we can find a common source for this..
	 */
	source = phi_parent(bb1, p1);
	if (phi_parent(bb2, p2) != source)
		return;

	/*
	 * Cool. We now know that 'source' is the exclusive
	 * parent of both phi-nodes, so the exit at the
	 * end of it fully determines which one it is, and
	 * we can turn it into a select.
	 *
	 * HOWEVER, right now we only handle regular
	 * conditional branches. No multijumps or computed
	 * stuff. Verify that here.
	 */
	br = last_instruction(source->insns);
	if (!br || br->opcode != OP_BR)
		return;

	/*
	 * We're in business. Match up true/false with p1/p2.
	 */
	if (br->bb_true == bb2 || br->bb_false == bb1) {
		pseudo_t p = p1;
		p1 = p2;
		p2 = p;
	}

	/*
	 * Ok, we can now replace that last
	 *
	 *	br cond, a, b
	 *
	 * with the sequence
	 *
	 *	setcc cond
	 *	select pseudo, p1, p2
	 *	br cond, a, b
	 *
	 * and remove the phi-node. If it then
	 * turns out that 'a' or 'b' is entirely
	 * empty (common case), and now no longer
	 * a phi-source, we'll be able to simplify
	 * the conditional branch too.
	 */
	insert_select(source, br, insn, p1, p2);
	clear_phi(insn);
	cse_repeat = 1;
}

static unsigned long clean_up_phi(struct instruction *insn)
{
	pseudo_t phi;
	struct instruction *last;
	unsigned long hash = 0;
	int same;

	last = NULL;
	same = 1;
	FOR_EACH_PTR(insn->phi_list, phi) {
		struct instruction *def = phi->def;
		if (def->src1 == VOID)
			continue;
		if (last) {
			if (last->src1 != def->src1)
				same = 0;
			continue;
		}
		last = def;
		hash += hashval(def->src1);
		hash += hashval(def->bb);
	} END_FOR_EACH_PTR(phi);

	if (same) {
		pseudo_t pseudo = last ? last->src1 : VOID;
		convert_instruction_target(insn, pseudo);
		cse_repeat = 1;
		insn->bb = NULL;
		/* This one is bogus, but no worse than anything else */
		return hash;
	}

	if_convert_phi(insn);

	return hash;
}

static void try_to_kill(struct instruction *);

static void kill_use(pseudo_t pseudo)
{
	if (pseudo->type == PSEUDO_REG) {
		if (ptr_list_size((struct ptr_list *)pseudo->users) == 1) {
			try_to_kill(pseudo->def);
		}
	}
}

static void try_to_kill(struct instruction *insn)
{
	int opcode = insn->opcode;
	switch (opcode) {
	case OP_BINARY ... OP_BINCMP_END:
		insn->bb = NULL;
		kill_use(insn->src1);
		kill_use(insn->src2);
		return;
	case OP_NOT: case OP_NEG:
		insn->bb = NULL;
		kill_use(insn->src1);
		return;

	case OP_PHI: case OP_SETVAL:
		insn->bb = NULL;
		return;
	}
}

/*
 * Kill trivially dead instructions
 */
static int dead_insn(struct instruction *insn, pseudo_t src1, pseudo_t src2)
{
	if (!insn->target->users) {
		insn->bb = NULL;
		kill_use(src1);
		kill_use(src2);
		return 1;
	}
	return 0;
}

static void clean_up_one_instruction(struct basic_block *bb, struct instruction *insn)
{
	unsigned long hash;

	if (!insn->bb)
		return;
	if (insn->bb != bb)
		warning(bb->pos, "instruction with bad bb");
	hash = insn->opcode;
	switch (insn->opcode) {	

	/* Binary arithmetic */
	case OP_ADD: case OP_SUB:
	case OP_MUL: case OP_DIV:
	case OP_MOD: case OP_SHL:
	case OP_SHR: case OP_SEL:
	case OP_AND: case OP_OR:

	/* Binary logical */
	case OP_XOR: case OP_AND_BOOL:
	case OP_OR_BOOL:

	/* Binary comparison */
	case OP_SET_EQ: case OP_SET_NE:
	case OP_SET_LE: case OP_SET_GE:
	case OP_SET_LT: case OP_SET_GT:
	case OP_SET_B:  case OP_SET_A:
	case OP_SET_BE: case OP_SET_AE:
		if (dead_insn(insn, insn->src1, insn->src2))
			return;
		hash += hashval(insn->src1);
		hash += hashval(insn->src2);
		break;
	
	/* Unary */
	case OP_NOT: case OP_NEG:
		if (dead_insn(insn, insn->src1, VOID))
			return;
		hash += hashval(insn->src1);
		break;

	case OP_SETVAL:
		if (dead_insn(insn, VOID, VOID))
			return;
		hash += hashval(insn->val);
		hash += hashval(insn->symbol);
		break;

	/* Other */
	case OP_PHI:
		if (dead_insn(insn, VOID, VOID)) {
			clear_phi(insn);
			return;
		}
		hash += clean_up_phi(insn);
		break;

	case OP_PHISOURCE:
		hash += hashval(insn->src1);
		hash += hashval(insn->bb);
		break;

	default:
		/*
		 * Nothing to do, don't even bother hashing them,
		 * we're not going to try to CSE them
		 */
		return;
	}
	hash += hash >> 16;
	hash &= INSN_HASH_SIZE-1;
	add_instruction(insn_hash_table + hash, insn);
}

static void clean_up_insns(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		FOR_EACH_PTR(bb->insns, insn) {
			clean_up_one_instruction(bb, insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}

extern void show_instruction(struct instruction *);

/* Compare two (sorted) phi-lists */
static int phi_list_compare(struct pseudo_list *l1, struct pseudo_list *l2)
{
	pseudo_t phi1, phi2;

	PREPARE_PTR_LIST(l1, phi1);
	PREPARE_PTR_LIST(l2, phi2);
	for (;;) {
		int cmp;

		if (!phi1)
			return phi2 ? -1 : 0;
		if (!phi2)
			return phi1 ? 1 : 0;
		cmp = phi_compare(phi1, phi2);
		if (cmp)
			return cmp;
		NEXT_PTR_LIST(phi1);
		NEXT_PTR_LIST(phi2);
	}
	/* Not reached, but we need to make the nesting come out right */
	FINISH_PTR_LIST(phi2);
	FINISH_PTR_LIST(phi1);
}

static int insn_compare(const void *_i1, const void *_i2)
{
	const struct instruction *i1 = _i1;
	const struct instruction *i2 = _i2;

	if (i1->opcode != i2->opcode)
		return i1->opcode < i2->opcode ? -1 : 1;

	switch (i1->opcode) {

	/* Binary arithmetic */
	case OP_ADD: case OP_SUB:
	case OP_MUL: case OP_DIV:
	case OP_MOD: case OP_SHL:
	case OP_SHR: case OP_SEL:
	case OP_AND: case OP_OR:

	/* Binary logical */
	case OP_XOR: case OP_AND_BOOL:
	case OP_OR_BOOL:

	/* Binary comparison */
	case OP_SET_EQ: case OP_SET_NE:
	case OP_SET_LE: case OP_SET_GE:
	case OP_SET_LT: case OP_SET_GT:
	case OP_SET_B:  case OP_SET_A:
	case OP_SET_BE: case OP_SET_AE:
		if (i1->src1 != i2->src1)
			return i1->src1 < i2->src1 ? -1 : 1;
		if (i1->src2 != i2->src2)
			return i1->src2 < i2->src2 ? -1 : 1;
		break;

	/* Unary */
	case OP_NOT: case OP_NEG:
		if (i1->src1 != i2->src1)
			return i1->src1 < i2->src1 ? -1 : 1;
		break;

	case OP_SETVAL:
		if (i1->val != i2->val)
			return i1->val < i2->val ? -1 : 1;
		if (i1->symbol != i2->symbol)
			return i1->symbol < i2->symbol ? -1 : 1;
		break;

	/* Other */
	case OP_PHI:
		return phi_list_compare(i1->phi_list, i2->phi_list);

	case OP_PHISOURCE:
		if (i1->src1 != i2->src1)
			return i1->src1 < i2->src1 ? -1 : 1;
		if (i1->bb != i2->bb)
			return i1->bb < i2->bb ? -1 : 1;
		break;

	default:
		warning(i1->bb->pos, "bad instruction on hash chain");
	}
	return 0;
}

static void sort_instruction_list(struct instruction_list **list)
{
	sort_list((struct ptr_list **)list , insn_compare);
}

static struct instruction * cse_one_instruction(struct instruction *insn, struct instruction *def)
{
	convert_instruction_target(insn, def->target);
	insn->opcode = OP_NOP;
	cse_repeat = 1;
	return def;
}

/*
 * Does "bb1" dominate "bb2"?
 */
static int bb_dominates(struct entrypoint *ep, struct basic_block *bb1, struct basic_block *bb2, unsigned long generation)
{
	struct basic_block *parent;

	/* Nothing dominates the entrypoint.. */
	if (bb2 == ep->entry)
		return 0;
	FOR_EACH_PTR(bb2->parents, parent) {
		if (parent == bb1)
			continue;
		if (parent->generation == generation)
			continue;
		parent->generation = generation;
		if (!bb_dominates(ep, bb1, parent, generation))
			return 0;
	} END_FOR_EACH_PTR(parent);
	return 1;
}

static struct instruction * try_to_cse(struct entrypoint *ep, struct instruction *i1, struct instruction *i2)
{
	struct basic_block *b1, *b2;

	/*
	 * Ok, i1 and i2 are the same instruction, modulo "target".
	 * We should now see if we can combine them.
	 */
	b1 = i1->bb;
	b2 = i2->bb;

	/*
	 * Currently we only handle the uninteresting degenerate case where
	 * the CSE is inside one basic-block.
	 */
	if (b1 == b2) {
		struct instruction *insn;
		FOR_EACH_PTR(b1->insns, insn) {
			if (insn == i1)
				return cse_one_instruction(i2, i1);
			if (insn == i2)
				return cse_one_instruction(i1, i2);
		} END_FOR_EACH_PTR(insn);
		warning(b1->pos, "Whaa? unable to find CSE instructions");
		return i1;
	}
	if (bb_dominates(ep, b1, b2, ++bb_generation))
		return cse_one_instruction(i2, i1);

	if (bb_dominates(ep, b2, b1, ++bb_generation))
		return cse_one_instruction(i1, i2);

	/* No direct dominance - but we could try to find a common ancestor.. */
	return i1;
}

void cleanup_and_cse(struct entrypoint *ep)
{
	int i;

repeat:
	cse_repeat = 0;
	clean_up_insns(ep);
	for (i = 0; i < INSN_HASH_SIZE; i++) {
		struct instruction_list **list = insn_hash_table + i;
		if (*list) {
			if (instruction_list_size(*list) > 1) {
				struct instruction *insn, *last;

				sort_instruction_list(list);

				last = NULL;
				FOR_EACH_PTR(*list, insn) {
					if (!insn->bb)
						continue;
					if (last) {
						if (!insn_compare(last, insn))
							insn = try_to_cse(ep, last, insn);
					}
					last = insn;
				} END_FOR_EACH_PTR(insn);
			}
			free_ptr_list((struct ptr_list **)list);
		}
	}

	if (cse_repeat)
		goto repeat;
}
