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

static inline int trackable_pseudo(pseudo_t pseudo)
{
	return pseudo && (pseudo->type == PSEUDO_REG || pseudo->type == PSEUDO_PHI);
}

static void insn_uses(struct entrypoint *ep, struct instruction *insn, pseudo_t pseudo)
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
		add_instruction(&pseudo->insns, insn);
	}
}

static void insn_defines(struct entrypoint *ep, struct instruction *insn, pseudo_t pseudo)
{
	assert(trackable_pseudo(pseudo));
	add_pseudo(&ep->pseudos, pseudo);
}

static void track_instruction_usage(struct entrypoint *ep, struct instruction *insn)
{
	pseudo_t pseudo;

	#define USES(x) insn_uses(ep, insn, insn->x)
	#define DEFINES(x) insn_defines(ep, insn, insn->x)

	switch (insn->opcode) {
	case OP_RET:
		USES(src);
		break;

	case OP_BR: case OP_SWITCH:
		USES(cond);
		break;

	case OP_COMPUTEDGOTO:
		USES(target);
	
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
		DEFINES(target);
		FOR_EACH_PTR(insn->phi_list, pseudo) {
			insn_uses(ep, insn, pseudo);
		} END_FOR_EACH_PTR(pseudo);
		break;

	case OP_PHISOURCE:
		USES(src1); DEFINES(target);
		break;

	case OP_CAST:
		USES(src); DEFINES(target);
		break;

	case OP_CALL:
		USES(func);
		if (insn->target != VOID)
			DEFINES(target);
		FOR_EACH_PTR(insn->arguments, pseudo) {
			insn_uses(ep, insn, pseudo);
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

void track_pseudo_usage(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		FOR_EACH_PTR(bb->insns, insn) {
			if (!insn->bb)
				continue;
			assert(insn->bb == bb);
			track_instruction_usage(ep, insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
}
