/*
 * UnSSA - translate the SSA back to normal form.
 *
 * For now it's done by replacing to set of copies:
 * 1) For each phi-node, replace all their phisrc by copies to a common
 *    temporary.
 * 2) Replace all the phi-nodes by copies of the temporaries to the phi-node target.
 *    This is node to preserve the semantic of the phi-node (they should all "execute"
 *    simultaneously on entry in the basic block in which they belong).
 *
 * This is similar to the "Sreedhar method I" except that the copies to the
 * temporaries are not placed at the end of the predecessor basic blocks, but
 * at the place where the phi-node operands are defined (the same place where the
 * phisrc were present).
 * Is this a problem? 
 *
 * While very simple this method create a lot more copies that really necessary.
 * Ideally, "Sreedhar method III" should be used:
 * "Translating Out of Static Single Assignment Form", V. C. Sreedhar, R. D.-C. Ju,
 * D. M. Gillies and V. Santhanam.  SAS'99, Vol. 1694 of Lecture Notes in Computer
 * Science, Springer-Verlag, pp. 194-210, 1999.
 *
 * Copyright (C) 2005 Luc Van Oostenryck
 */

#include "lib.h"
#include "linearize.h"
#include "allocate.h"
#include "flow.h"
#include <assert.h>


static void remove_phisrc_defines(struct instruction *phisrc)
{
	struct instruction *phi;
	struct basic_block *bb = phisrc->bb;

	FOR_EACH_PTR(phisrc->phi_users, phi) {
		remove_pseudo(&bb->defines, phi->target);
	} END_FOR_EACH_PTR(phi);
}

static void replace_phi_node(struct instruction *phi)
{
	pseudo_t tmp;

	tmp = alloc_pseudo(NULL);
	tmp->type = phi->target->type;
	tmp->ident = phi->target->ident;
	tmp->def = NULL;		// defined by all the phisrc
	
	// update the current liveness
	remove_pseudo(&phi->bb->needs, phi->target);
	add_pseudo(&phi->bb->needs, tmp);
	track_phi_uses(phi);

	phi->opcode = OP_COPY;
	phi->src = tmp;

	// FIXME: free phi->phi_list;
}

static void rewrite_phi_bb(struct basic_block *bb)
{
	struct instruction *insn;

	// Replace all the phi-nodes by copies of a temporary
	// (which represent the set of all the %phi that feed them).
	// The target pseudo doesn't change.
	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		if (insn->opcode != OP_PHI)
			continue;
		replace_phi_node(insn);
	} END_FOR_EACH_PTR(insn);
}

static void rewrite_phisrc_bb(struct basic_block *bb)
{
	struct instruction *insn;

	// Replace all the phisrc by one or several copies to
	// the temporaries associated to each phi-node it defines.
	FOR_EACH_PTR_REVERSE(bb->insns, insn) {
		struct instruction *phi;
		int i;

		if (!insn->bb)
			continue;
		if (insn->opcode != OP_PHISOURCE)
			continue;

		i = 0;
		FOR_EACH_PTR(insn->phi_users, phi) {
			pseudo_t tmp = phi->src;
			pseudo_t src = insn->phi_src;

			if (i == 0) {	// first phi: we overwrite the phisrc
				insn->opcode = OP_COPY;
				insn->target = tmp;
				insn->src = src;
			} else {
				struct instruction *copy = __alloc_instruction(0);

				copy->bb = bb;
				copy->opcode = OP_COPY;
				copy->size = insn->size;
				copy->pos = insn->pos;
				copy->target = tmp;
				copy->src = src;

				INSERT_CURRENT(copy, insn);
			}
			// update the liveness info
			remove_phisrc_defines(insn);
			// FIXME: should really something like add_pseudo_exclusive()
			add_pseudo(&bb->defines, tmp);

			i++;
		} END_FOR_EACH_PTR(phi);

	} END_FOR_EACH_PTR_REVERSE(insn);
}

int unssa(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		rewrite_phi_bb(bb);
	} END_FOR_EACH_PTR(bb);

	FOR_EACH_PTR(ep->bbs, bb) {
		rewrite_phisrc_bb(bb);
	} END_FOR_EACH_PTR(bb);

	return 0;
}
