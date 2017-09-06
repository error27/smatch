// SPDX-License-Identifier: MIT

#include "ir.h"
#include "linearize.h"
#include <stdlib.h>


static int validate_insn(struct instruction *insn)
{
	int err = 0;

	switch (insn->opcode) {
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
