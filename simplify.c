/*
 * Simplify - do instruction simplification before CSE
 *
 * Copyright (C) 2004 Linus Torvalds
 */

#include <assert.h>

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

	assert(br->cond);
	assert(br->bb_false);

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
		struct instruction *def;
		if (phi == VOID)
			continue;
		def = phi->def;
		if (def->src1 == VOID || !def->bb)
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
		clear_phi(insn);
		return 1;
	}

	return if_convert_phi(insn);
}

static inline void remove_usage(pseudo_t p, pseudo_t *usep)
{
	if (p && p->type != PSEUDO_VOID && p->type != PSEUDO_VAL) {
		int deleted;
		deleted = delete_ptr_list_entry((struct ptr_list **)&p->users, usep);
		assert(deleted == 1);
		if (!p->users)
			kill_instruction(p->def);
	}
}

void kill_use(pseudo_t *usep)
{
	if (usep) {
		pseudo_t p = *usep;
		*usep = VOID;
		remove_usage(p, usep);
	}
}

void kill_instruction(struct instruction *insn)
{
	if (!insn || !insn->bb)
		return;

	switch (insn->opcode) {
	case OP_BINARY ... OP_BINCMP_END:
		insn->bb = NULL;
		kill_use(&insn->src1);
		kill_use(&insn->src2);
		return;
	case OP_NOT: case OP_NEG:
		insn->bb = NULL;
		kill_use(&insn->src1);
		return;

	case OP_PHI: case OP_SETVAL:
		insn->bb = NULL;
		return;
	}
}

/*
 * Kill trivially dead instructions
 */
static int dead_insn(struct instruction *insn, pseudo_t *src1, pseudo_t *src2)
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
	long long value = insn->src2->value;

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
	/* FIXME! Verify signs and sizes!! */
	long long left = insn->src1->value;
	long long right = insn->src2->value;
	long long res, mask;

	switch (insn->opcode) {
	case OP_ADD:
		res = left + right;
		break;
	case OP_SUB:
		res = left - right;
		break;
	case OP_MUL:
		/* FIXME! Check sign! */
		res = left * right;
		break;
	case OP_DIV:
		if (!right)
			return 0;
		/* FIXME! Check sign! */
		res = left / right;
		break;
	case OP_MOD:
		if (!right)
			return 0;
		/* FIXME! Check sign! */
		res = left % right;
		break;
	case OP_SHL:
		res = left << right;
		break;
	case OP_SHR:
		/* FIXME! Check sign! */
		res = left >> right;
		break;
       /* Logical */
	case OP_AND:
		res = left & right;
		break;
	case OP_OR:
		res = left | right;
		break;
	case OP_XOR:
		res = left ^ right;
		break;
	case OP_AND_BOOL:
		res = left && right;
		break;
	case OP_OR_BOOL:
		res = left || right;
		break;
			       
	/* Binary comparison */
	case OP_SET_EQ:
		res = left == right;
		break;
	case OP_SET_NE:
		res = left != right;
		break;
	case OP_SET_LE:
		/* FIXME! Check sign! */
		res = left <= right;
		break;
	case OP_SET_GE:
		/* FIXME! Check sign! */
		res = left >= right;
		break;
	case OP_SET_LT:
		/* FIXME! Check sign! */
		res = left < right;
		break;
	case OP_SET_GT:
		/* FIXME! Check sign! */
		res = left > right;
		break;
	case OP_SET_B:
		/* FIXME! Check sign! */
		res = (unsigned long long) left < (unsigned long long) right;
		break;
	case OP_SET_A:
		/* FIXME! Check sign! */
		res = (unsigned long long) left > (unsigned long long) right;
		break;
	case OP_SET_BE:
		/* FIXME! Check sign! */
		res = (unsigned long long) left <= (unsigned long long) right;
		break;
	case OP_SET_AE:
		/* FIXME! Check sign! */
		res = (unsigned long long) left >= (unsigned long long) right;
		break;
	default:
		return 0;
	}
	mask = 1ULL << (insn->type->bit_size-1);
	res &= mask | (mask-1);

	/* FIXME!! Sign??? */
	replace_with_pseudo(insn, value_pseudo(res));
	return 1;
}

static int simplify_binop(struct instruction *insn)
{
	if (dead_insn(insn, &insn->src1, &insn->src2))
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
	if (dead_insn(insn, &insn->src1, NULL))
		return 1;
	if (constant(insn->src1))
		return simplify_constant_unop(insn);
	return 0;
}

static int simplify_memop(struct instruction *insn)
{
	pseudo_t addr = insn->src;
	if (addr->type == PSEUDO_REG) {
		struct instruction *def = addr->def;
		if (def->opcode == OP_SETVAL && def->src) {
			kill_use(&insn->src);
			use_pseudo(def->src, &insn->src);
			return 1;
		}
	}
	return 0;
}

int simplify_instruction(struct instruction *insn)
{
	pseudo_t cond;
	static struct instruction *last_setcc;
	struct instruction *setcc = last_setcc;

	last_setcc = NULL;

	if (!insn->bb)
		return 0;
	switch (insn->opcode) {
	case OP_BINARY ... OP_BINCMP_END:
		return simplify_binop(insn);

	case OP_NOT: case OP_NEG:
		return simplify_unop(insn);
	case OP_LOAD: case OP_STORE:
		return simplify_memop(insn);
	case OP_SETVAL:
		if (dead_insn(insn, NULL, NULL))
			return 1;
		break;
	case OP_PHI:
		if (dead_insn(insn, NULL, NULL)) {
			clear_phi(insn);
			return 1;
		}
		return clean_up_phi(insn);
	case OP_PHISOURCE:
		if (dead_insn(insn, &insn->src1, NULL))
			return 1;
		break;
	case OP_SETCC:
		last_setcc = insn;
		return 0;
	case OP_SEL:
		assert(setcc && setcc->bb);
		if (dead_insn(insn, &insn->src1, &insn->src2)) {
			setcc->bb = NULL;
			return 1;
		}
		cond = setcc->src;
		if (!constant(cond) && insn->src1 != insn->src2)
			return 0;
		setcc->bb = NULL;
		kill_use(&setcc->cond);
		replace_with_pseudo(insn, cond->value ? insn->src1 : insn->src2);
		return 1;
	case OP_BR:
		cond = insn->cond;
		if (!cond || !constant(cond))
			break;
		insert_branch(insn->bb, insn, cond->value ? insn->bb_true : insn->bb_false);
		return 1;
	}
	return 0;
}
