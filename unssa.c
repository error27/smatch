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
#include <assert.h>

static struct instruction *alloc_copy_insn(struct instruction *phisrc)
{
	struct instruction *insn = __alloc_instruction(0);
	insn->opcode = OP_COPY;
	insn->size = phisrc->size;
	insn->pos = phisrc->pos;
	return insn;
}

// Is there a better way to do this ???
static void insert_insn_before(struct instruction *old, struct instruction *new)
{
	struct instruction *insn;

	FOR_EACH_PTR_REVERSE(old->bb->insns, insn) {
		if (insn == old)
			INSERT_CURRENT(new, insn);
	}
	END_FOR_EACH_PTR_REVERSE(insn);
}

static void insert_copy(struct instruction *phisrc, pseudo_t tmp)
{
	struct instruction *cpy;

	assert(phisrc->opcode == OP_PHISOURCE);

	cpy =  alloc_copy_insn(phisrc);
	cpy->target = tmp;
	cpy->src = phisrc->phi_src;
	cpy->bb = phisrc->bb;
	insert_insn_before(phisrc, cpy);
}

static void copy_phi_args(struct instruction *phi, struct pseudo_list **list)
{
	pseudo_t tmp, orig = phi->target;
	pseudo_t phisrc;

	tmp = alloc_pseudo(NULL);
	tmp->type = orig->type;
	tmp->def = phi;			// wrongly set to the phi-node !!!
					// but used later
	add_pseudo(list, tmp);

	FOR_EACH_PTR(phi->phi_list, phisrc) {
		if (!phisrc->def)
			continue;
		insert_copy(phisrc->def, tmp);
	} END_FOR_EACH_PTR(phisrc);
}

static void unssa_bb(struct basic_block *bb)
{
	struct pseudo_list *list = NULL;
	struct pseudo *tmp;
	struct instruction *insn;

	// copy all the phi nodes arguments to a new temporary pseudo
	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->opcode != OP_PHI)
			continue;
		copy_phi_args(insn, &list);
	} END_FOR_EACH_PTR(insn);

	// now replace all the phi nodes themselves by copies of the
	// temporaries to the phi nodes targets
	FOR_EACH_PTR(list, tmp) {
		struct instruction *phi = tmp->def;

		phi->opcode = OP_COPY;
		phi->src = tmp;
	} END_FOR_EACH_PTR(tmp);
	free_ptr_list(&list);
}

int unssa(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		unssa_bb(bb);
	} END_FOR_EACH_PTR(bb);

	return 0;
}
