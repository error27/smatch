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

static int phi_compare(const void *_phi1, const void *_phi2)
{
	const struct phi *phi1 = _phi1;
	const struct phi *phi2 = _phi2;

	if (phi1->pseudo != phi2->pseudo)
		return phi1->pseudo < phi2->pseudo ? -1 : 1;
	if (phi1->source != phi2->source)
		return phi1->source < phi2->source ? -1 : 1;
	return 0;
}

static void sort_phi_list(struct phi_list **list)
{
	sort_list((struct ptr_list **)list , phi_compare);
}

static unsigned long clean_up_phi(struct instruction *insn)
{
	struct phi *phi, *last;
	unsigned long hash = 0;

	sort_phi_list(&insn->phi_list);

	last = NULL;
	FOR_EACH_PTR(insn->phi_list, phi) {
		if (last) {
			if (last->pseudo == phi->pseudo &&
			    last->source == phi->source) {
				DELETE_CURRENT_PTR(phi);
				continue;
			}
		}
		last = phi;
		hash += hashval(phi->pseudo);
		hash += hashval(phi->source);
	} END_FOR_EACH_PTR(phi);

	/* Whenever we delete pointers, we may have to pack the end result */
	PACK_PTR_LIST(&insn->phi_list);
	return hash;
}

static void clean_up_one_instruction(struct basic_block *bb, struct instruction *insn)
{
	unsigned long hash;

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
		hash += hashval(insn->src1);
		hash += hashval(insn->src2);
		break;
	
	/* Unary */
	case OP_NOT: case OP_NEG:
		hash += hashval(insn->src1);
		break;

	case OP_SETVAL:
		hash += hashval(insn->val);
		hash += hashval(insn->symbol);
		break;

	/* Other */
	case OP_PHI:
		hash += clean_up_phi(insn);
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
static int phi_list_compare(struct phi_list *l1, struct phi_list *l2)
{
	struct phi *phi1, *phi2;

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
	/* We re-use the "convert_load_insn()" function. Does the same thing */
	convert_load_insn(insn, def->target);
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
		return NULL;
	}
	if (bb_dominates(ep, b1, b2, ++bb_generation))
		return cse_one_instruction(i2, i1);

	if (bb_dominates(ep, b2, b1, ++bb_generation))
		return cse_one_instruction(i1, i2);

	/* No direct dominance - but we could try to find a common ancestor.. */
	return NULL;
}

void cleanup_and_cse(struct entrypoint *ep)
{
	int i, success;

repeat:
	success = 0;
	clean_up_insns(ep);
	for (i = 0; i < INSN_HASH_SIZE; i++) {
		struct instruction_list **list = insn_hash_table + i;
		if (*list) {
			if (instruction_list_size(*list) > 1) {
				struct instruction *insn, *last;

				sort_instruction_list(list);

				last = NULL;
				FOR_EACH_PTR(*list, insn) {
					if (last) {
						if (!insn_compare(last, insn)) {
							struct instruction *def = try_to_cse(ep, last, insn);
							if (def) {
								success++;
								insn = def;
							}
						}
					}
					last = insn;
				} END_FOR_EACH_PTR(insn);
			}
			free_ptr_list((struct ptr_list **)list);
		}
	}

	if (success)
		goto repeat;
}
