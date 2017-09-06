// SPDX-License-Identifier: MIT

#include "ir.h"
#include "linearize.h"
#include <stdlib.h>


static int check_user(struct instruction *insn, pseudo_t pseudo)
{
	struct instruction *def;

	if (!pseudo) {
		show_entry(insn->bb->ep);
		sparse_error(insn->pos, "null pseudo in %s", show_instruction(insn));
		return 1;
	}
	switch (pseudo->type) {
	case PSEUDO_PHI:
	case PSEUDO_REG:
		def = pseudo->def;
		if (def && def->bb)
			break;
		show_entry(insn->bb->ep);
		sparse_error(insn->pos, "wrong usage for %s in %s", show_pseudo(pseudo),
			show_instruction(insn));
		return 1;

	default:
		break;
	}
	return 0;
}

static int validate_insn(struct instruction *insn)
{
	int err = 0;

	switch (insn->opcode) {
	case OP_SEL:
	case OP_RANGE:
		err += check_user(insn, insn->src3);
		/* fall through */

	case OP_BINARY ... OP_BINCMP_END:
		err += check_user(insn, insn->src2);
		/* fall through */

	case OP_CAST:
	case OP_SCAST:
	case OP_FPCAST:
	case OP_PTRCAST:
	case OP_NOT: case OP_NEG:
	case OP_SLICE:
	case OP_SYMADDR:
	case OP_PHISOURCE:
		err += check_user(insn, insn->src1);
		break;

	case OP_CBR:
	case OP_COMPUTEDGOTO:
		err += check_user(insn, insn->cond);
		break;

	case OP_PHI:
		break;

	case OP_CALL:
		// FIXME: ignore for now
		break;

	case OP_STORE:
		err += check_user(insn, insn->target);
		/* fall through */

	case OP_LOAD:
		err += check_user(insn, insn->src);
		break;

	case OP_ENTRY:
	case OP_BR:
	case OP_SETVAL:
	default:
		break;
	}

	return err;
}

int ir_validate(struct entrypoint *ep)
{
	struct basic_block *bb;
	int err = 0;

	if (!dbg_ir || has_error)
		return 0;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		FOR_EACH_PTR(bb->insns, insn) {
			if (!insn->bb)
				continue;
			err += validate_insn(insn);
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);

	if (err)
		abort();
	return err;
}
