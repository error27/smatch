/*
 * Simplify - do instruction simplification before CSE
 *
 * Copyright (C) 2004 Linus Torvalds
 */

#include "parse.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"


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
		*THIS_ADDRESS(phi) = VOID;
	} END_FOR_EACH_PTR(phi);
}

static int if_convert_phi(struct instruction *insn)
{
	pseudo_t array[3];
	struct basic_block *parents[3];
	struct basic_block *bb, *bb1, *bb2, *source;
	struct instruction *br;
	pseudo_t p1, p2;

	bb = insn->bb;
	if (linearize_ptr_list((struct ptr_list *)insn->phi_list, (void **)array, 3) != 2)
		return 0;
	if (linearize_ptr_list((struct ptr_list *)bb->parents, (void **)parents, 3) != 2)
		return 0;
	p1 = array[0]->def->src1;
	bb1 = array[0]->def->bb;
	p2 = array[1]->def->src1;
	bb2 = array[1]->def->bb;

	/* Only try the simple "direct parents" case */
	if ((bb1 != parents[0] || bb2 != parents[1]) &&
	    (bb1 != parents[1] || bb2 != parents[0]))
		return 0;

	/*
	 * See if we can find a common source for this..
	 */
	source = phi_parent(bb1, p1);
	if (phi_parent(bb2, p2) != source)
		return 0;

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
		return 0;

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
	return 1;
}

static int clean_up_phi(struct instruction *insn)
{
	pseudo_t phi;
	struct instruction *last;
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
	} END_FOR_EACH_PTR(phi);

	if (same) {
		pseudo_t pseudo = last ? last->src1 : VOID;
		convert_instruction_target(insn, pseudo);
		insn->bb = NULL;
		return 1;
	}

	return if_convert_phi(insn);
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
	pseudo_t *usep;
	FOR_EACH_PTR(insn->target->users, usep) {
		if (*usep != VOID)
			return 0;
	} END_FOR_EACH_PTR(usep);

	insn->bb = NULL;
	kill_use(src1);
	kill_use(src2);
	return 1;
}

static inline int constant(pseudo_t pseudo)
{
	return pseudo->type == PSEUDO_VAL;
}

static int replace_with_pseudo(struct instruction *insn, pseudo_t pseudo)
{
	convert_instruction_target(insn, pseudo);
	insn->bb = NULL;
	return 1;
}

static int simplify_constant_rightside(struct instruction *insn)
{
	unsigned long value = insn->src2->value;

	switch (insn->opcode) {
	case OP_ADD: case OP_SUB:
	case OP_OR: case OP_XOR:
	case OP_SHL: case OP_SHR:
		if (!value)
			return replace_with_pseudo(insn, insn->src1);
		return 0;

	case OP_AND: case OP_MUL:
		if (!value)
			return replace_with_pseudo(insn, insn->src2);
		return 0;
	}
	return 0;
}

static int simplify_constant_leftside(struct instruction *insn)
{
	return 0;
}

static int simplify_constant_binop(struct instruction *insn)
{
	return simplify_constant_rightside(insn);
}

static int simplify_binop(struct instruction *insn)
{
	if (dead_insn(insn, insn->src1, insn->src2))
		return 1;
	if (constant(insn->src1)) {
		if (constant(insn->src2))
			return simplify_constant_binop(insn);
		return simplify_constant_leftside(insn);
	}
	if (constant(insn->src2))
		return simplify_constant_rightside(insn);
	return 0;
}

static int simplify_constant_unop(struct instruction *insn)
{
	return 0;
}

static int simplify_unop(struct instruction *insn)
{
	if (dead_insn(insn, insn->src1, VOID))
		return 1;
	if (constant(insn->src1))
		return simplify_constant_unop(insn);
	return 0;
}

int simplify_instruction(struct instruction *insn)
{
	switch (insn->opcode) {
	case OP_BINARY ... OP_BINCMP_END:
		return simplify_binop(insn);

	case OP_NOT: case OP_NEG:
		return simplify_unop(insn);

	case OP_SETVAL:
		if (dead_insn(insn, VOID, VOID))
			return 1;
		break;
	case OP_PHI:
		if (dead_insn(insn, VOID, VOID)) {
			clear_phi(insn);
			return 1;
		}
		return clean_up_phi(insn);
	case OP_PHISOURCE:
		if (dead_insn(insn, insn->src1, VOID))
			return 1;
		break;
	}
	return 0;
}
