/*
 * Register - track pseudo usage, maybe eventually try to do register
 * allocation.
 *
 * Copyright (C) 2004 Linus Torvalds
 */

#include <assert.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"

int pseudo_in_list(struct pseudo_list *list, pseudo_t pseudo)
{
	pseudo_t old;
	FOR_EACH_PTR(list,old) {
		if (old == pseudo)
			return 1;
	} END_FOR_EACH_PTR(old);   
	return 0;
}

static int liveness_changed;

static void add_pseudo_exclusive(struct pseudo_list **list, pseudo_t pseudo)
{
	if (!pseudo_in_list(*list, pseudo)) {
		liveness_changed = 1;
		add_pseudo(list, pseudo);
	}
}

static inline int trackable_pseudo(pseudo_t pseudo)
{
	return pseudo && (pseudo->type == PSEUDO_REG || pseudo->type == PSEUDO_PHI);
}

static void insn_uses(struct basic_block *bb, struct instruction *insn, pseudo_t pseudo)
{
	if (trackable_pseudo(pseudo)) {
		struct instruction *def = pseudo->def;
#if 1
		/*
		 * This assert is wrong, since this actually _can_ happen for
		 * truly undefined programs, but it's good for finding bugs.
		 */
		assert(def && def->bb);
#else
		if (!def || !def->bb)
			warning(insn->bb->pos, "undef pseudo ;(");
#endif
		add_pseudo_exclusive(&bb->needs, pseudo);
	}
}

static void insn_defines(struct basic_block *bb, struct instruction *insn, pseudo_t pseudo)
{
	assert(trackable_pseudo(pseudo));
	add_pseudo(&bb->defines, pseudo);
}

static void phi_defines(struct instruction * phi_node, pseudo_t target)
{
	pseudo_t phi;
	FOR_EACH_PTR(phi_node->phi_list, phi) {
		struct instruction *def;
		if (phi == VOID)
			continue;
		def = phi->def;
		if (!def || !def->bb)
			continue;
		if (def->opcode == OP_PHI) {
			phi_defines(def, target);
			continue;
		}
		insn_defines(def->bb, phi->def, target);
	} END_FOR_EACH_PTR(phi);
}

static void track_instruction_usage(struct basic_block *bb, struct instruction *insn)
{
	pseudo_t pseudo;

	#define USES(x) insn_uses(bb, insn, insn->x)
	#define DEFINES(x) insn_defines(bb, insn, insn->x)

	switch (insn->opcode) {
	case OP_RET:
		USES(src);
		break;

	case OP_BR: case OP_SWITCH:
		USES(cond);
		break;

	case OP_COMPUTEDGOTO:
		USES(target);
		break;
	
	/* Binary */
	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		USES(src1); USES(src2); DEFINES(target);
		break;

	/* Uni */
	case OP_NOT: case OP_NEG:
		USES(src1); DEFINES(target);
		break;

	/* Setcc - always in combination with a select or conditional branch */
	case OP_SETCC:
		USES(src);
		break;

	case OP_SEL:
		USES(src1); USES(src2); DEFINES(target);
		break;
	
	/* Memory */
	case OP_LOAD:
		USES(src); DEFINES(target);
		break;

	case OP_STORE:
		USES(src); USES(target);
		break;

	case OP_SETVAL:
		USES(symbol); DEFINES(target);
		break;

	/* Other */
	case OP_PHI:
		/* Phi-nodes are "backwards" nodes. Their def doesn't matter */
		phi_defines(insn, insn->target);
		break;

	case OP_PHISOURCE:
		/*
		 * We don't care about the phi-source define, they get set
		 * up and expanded by the OP_PHI
		 */
		USES(src1);
		break;

	case OP_CAST:
	case OP_PTRCAST:
		USES(src); DEFINES(target);
		break;

	case OP_CALL:
		USES(func);
		if (insn->target != VOID)
			DEFINES(target);
		FOR_EACH_PTR(insn->arguments, pseudo) {
			insn_uses(bb, insn, pseudo);
		} END_FOR_EACH_PTR(pseudo);
		break;

	case OP_SLICE:
		USES(base); DEFINES(target);
		break;

	case OP_BADOP:
	case OP_INVOKE:
	case OP_UNWIND:
	case OP_MALLOC:
	case OP_FREE:
	case OP_ALLOCA:
	case OP_GET_ELEMENT_PTR:
	case OP_VANEXT:
	case OP_VAARG:
	case OP_SNOP:
	case OP_LNOP:
	case OP_NOP:
	case OP_CONTEXT:
		break;
	}
}

static void track_bb_liveness(struct basic_block *bb)
{
	pseudo_t needs;

	FOR_EACH_PTR(bb->needs, needs) {
		if (!pseudo_in_list(bb->defines, needs)) {
			struct basic_block *parent;
			FOR_EACH_PTR(bb->parents, parent) {
				if (!pseudo_in_list(parent->defines, needs)) {
					add_pseudo_exclusive(&parent->needs, needs);
				}
			} END_FOR_EACH_PTR(parent);
		}
	} END_FOR_EACH_PTR(needs);
}

static inline void remove_pseudo(struct pseudo_list **list, pseudo_t pseudo)
{
	delete_ptr_list_entry((struct ptr_list **)list, pseudo, 0);
}

/*
 * We need to clear the liveness information if we 
 * are going to re-run it.
 */
void clear_liveness(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		free_ptr_list(&bb->needs);
		free_ptr_list(&bb->defines);
	} END_FOR_EACH_PTR(bb);
}

/*
 * Track inter-bb pseudo liveness. The intra-bb case
 * is purely local information.
 */
void track_pseudo_liveness(struct entrypoint *ep)
{
	struct basic_block *bb;

	/* Add all the bb pseudo usage */
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		FOR_EACH_PTR(bb->insns, insn) {
			if (!insn->bb)
				continue;
			assert(insn->bb == bb);
			track_instruction_usage(bb, insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);

	/* Remove the pseudos from the "need" list that are defined internally */
	FOR_EACH_PTR(ep->bbs, bb) {
		pseudo_t def;
		FOR_EACH_PTR(bb->defines, def) {
			remove_pseudo(&bb->needs, def);
		} END_FOR_EACH_PTR(def);
	} END_FOR_EACH_PTR(bb);

	/* Calculate liveness.. */
	do {
		liveness_changed = 0;
		FOR_EACH_PTR(ep->bbs, bb) {
			track_bb_liveness(bb);
		} END_FOR_EACH_PTR(bb);
	} while (liveness_changed);

	/* Remove the pseudos from the "defines" list that are used internally */
	FOR_EACH_PTR(ep->bbs, bb) {
		pseudo_t def;
		FOR_EACH_PTR(bb->defines, def) {
			struct basic_block *child;
			FOR_EACH_PTR(bb->children, child) {
				if (pseudo_in_list(child->needs, def))
					goto is_used;
			} END_FOR_EACH_PTR(child);
			DELETE_CURRENT_PTR(def);
is_used:
		;
		} END_FOR_EACH_PTR(def);
		PACK_PTR_LIST(&bb->defines);
	} END_FOR_EACH_PTR(bb);
}
