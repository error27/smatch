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
	if (source != phi_parent(bb2, p2))
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
	return REPEAT_CSE;
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
		return REPEAT_CSE;
	}

	return if_convert_phi(insn);
}

static inline void remove_usage(pseudo_t p, pseudo_t *usep)
{
	if (has_use_list(p)) {
		delete_ptr_list_entry((struct ptr_list **)&p->users, usep, 1);
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
		repeat_phase |= REPEAT_CSE;
		return;

	case OP_NOT: case OP_NEG:
		insn->bb = NULL;
		kill_use(&insn->src1);
		repeat_phase |= REPEAT_CSE;
		return;

	case OP_PHI:
		insn->bb = NULL;
		repeat_phase |= REPEAT_CSE;
		return;

	 case OP_SETVAL:
	 	insn->bb = NULL;
	 	repeat_phase |= REPEAT_CSE;
	 	if (insn->symbol)
	 		repeat_phase |= REPEAT_SYMBOL_CLEANUP;
	 	return;
	}
}

/*
 * Kill trivially dead instructions
 */
static int dead_insn(struct instruction *insn, pseudo_t *src1, pseudo_t *src2, pseudo_t *src3)
{
	pseudo_t *usep;
	FOR_EACH_PTR(insn->target->users, usep) {
		if (*usep != VOID)
			return 0;
	} END_FOR_EACH_PTR(usep);

	insn->bb = NULL;
	kill_use(src1);
	kill_use(src2);
	kill_use(src3);
	return REPEAT_CSE;
}

static inline int constant(pseudo_t pseudo)
{
	return pseudo->type == PSEUDO_VAL;
}

static int replace_with_pseudo(struct instruction *insn, pseudo_t pseudo)
{
	convert_instruction_target(insn, pseudo);
	insn->bb = NULL;
	return REPEAT_CSE;
}

static int simplify_constant_rightside(struct instruction *insn)
{
	long long value = insn->src2->value;

	switch (insn->opcode) {
	case OP_SUB:
		if (value) {
			insn->opcode = OP_ADD;
			insn->src2 = value_pseudo(-value);
			return REPEAT_CSE;
		}
	/* Fallthrough */
	case OP_ADD:
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
	long long value = insn->src1->value;

	switch (insn->opcode) {
	case OP_ADD: case OP_OR: case OP_XOR:
		if (!value)
			return replace_with_pseudo(insn, insn->src2);
		return 0;

	case OP_SHL: case OP_SHR:
	case OP_AND: case OP_MUL:
		if (!value)
			return replace_with_pseudo(insn, insn->src1);
		return 0;
	}
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
	mask = 1ULL << (insn->size-1);
	res &= mask | (mask-1);

	/* FIXME!! Sign??? */
	replace_with_pseudo(insn, value_pseudo(res));
	return REPEAT_CSE;
}

static int simplify_binop(struct instruction *insn)
{
	if (dead_insn(insn, &insn->src1, &insn->src2, NULL))
		return REPEAT_CSE;
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
	long long val = insn->src1->value;
	long long res, mask;

	switch (insn->opcode) {
	case OP_NOT:
		res = ~val;
		break;
	case OP_NEG:
		res = -val;
		break;
	default:
		return 0;
	}
	mask = 1ULL << (insn->size-1);
	res &= mask | (mask-1);
	
	replace_with_pseudo(insn, value_pseudo(res));
	return REPEAT_CSE;
}

static int simplify_unop(struct instruction *insn)
{
	if (dead_insn(insn, &insn->src1, NULL, NULL))
		return REPEAT_CSE;
	if (constant(insn->src1))
		return simplify_constant_unop(insn);
	return 0;
}

static int simplify_one_memop(struct instruction *insn, pseudo_t orig)
{
	pseudo_t addr = insn->src;
	pseudo_t new, off;

	if (addr->type == PSEUDO_REG) {
		struct instruction *def = addr->def;
		if (def->opcode == OP_SETVAL && def->src) {
			kill_use(&insn->src);
			use_pseudo(def->src, &insn->src);
			return REPEAT_CSE | REPEAT_SYMBOL_CLEANUP;
		}
		if (def->opcode == OP_ADD) {
			new = def->src1;
			off = def->src2;
			if (constant(off))
				goto offset;
			new = off;
			off = def->src1;
			if (constant(off))
				goto offset;
			return 0;
		}
	}
	return 0;

offset:
	/* Invalid code */
	if (new == orig) {
		if (new == VOID)
			return 0;
		new = VOID;
		warning(insn->bb->pos, "crazy programmer");
	}
	insn->offset += off->value;
	use_pseudo(new, &insn->src);
	remove_usage(addr, &insn->src);
	return REPEAT_CSE | REPEAT_SYMBOL_CLEANUP;
}

/*
 * We walk the whole chain of adds/subs backwards. That's not
 * only more efficient, but it allows us to find looops.
 */
static int simplify_memop(struct instruction *insn)
{
	int one, ret = 0;
	pseudo_t orig = insn->src;

	do {
		one = simplify_one_memop(insn, orig);
		ret |= one;
	} while (one);
	return ret;
}

static int simplify_cast(struct instruction *insn)
{
	int orig_size;

	if (dead_insn(insn, &insn->src, NULL, NULL))
		return REPEAT_CSE;
	if (insn->opcode == OP_PTRCAST)
		return 0;
	orig_size = insn->orig_type ? insn->orig_type->bit_size : 0;
	if (orig_size < 0)
		orig_size = 0;
	if (insn->size != orig_size)
		return 0;
	return replace_with_pseudo(insn, insn->src);
}

static int simplify_select(struct instruction *insn)
{
	pseudo_t cond, src1, src2;

	if (dead_insn(insn, &insn->src1, &insn->src2, &insn->src3))
		return REPEAT_CSE;

	cond = insn->src1;
	src1 = insn->src2;
	src2 = insn->src3;
	if (constant(cond) || src1 == src2) {
		pseudo_t *kill, take;
		kill_use(&insn->src1);
		take = cond->value ? src1 : src2;
		kill = cond->value ? &insn->src3 : &insn->src2;
		kill_use(kill);
		replace_with_pseudo(insn, take);
		return REPEAT_CSE;
	}
	if (constant(src1) && constant(src2)) {
		long long val1 = src1->value;
		long long val2 = src2->value;

		/* The pair 0/1 is special - replace with SETNE/SETEQ */
		if ((val1 | val2) == 1) {
			int opcode = OP_SET_EQ;
			if (val1) {
				src1 = src2;
				opcode = OP_SET_NE;
			}
			insn->opcode = opcode;
			/* insn->src1 is already cond */
			insn->src2 = src1; /* Zero */
			return REPEAT_CSE;
		}
	}
	return 0;
}

/*
 * Simplify "set_ne/eq $0 + br"
 */
static int simplify_cond_branch(struct instruction *br, pseudo_t cond, struct instruction *def, pseudo_t *pp)
{
	use_pseudo(*pp, &br->cond);
	remove_usage(cond, &br->cond);
	if (def->opcode == OP_SET_EQ) {
		struct basic_block *true = br->bb_true;
		struct basic_block *false = br->bb_false;
		br->bb_false = true;
		br->bb_true = false;
	}
	return REPEAT_CSE;
}

static int simplify_branch(struct instruction *insn)
{
	pseudo_t cond = insn->cond;

	if (!cond)
		return 0;

	/* Constant conditional */
	if (constant(cond)) {
		insert_branch(insn->bb, insn, cond->value ? insn->bb_true : insn->bb_false);
		return REPEAT_CSE;
	}

	/* Same target? */
	if (insn->bb_true == insn->bb_false) {
		struct basic_block *bb = insn->bb;
		struct basic_block *target = insn->bb_false;
		remove_bb_from_list(&target->parents, bb, 1);
		remove_bb_from_list(&bb->children, target, 1);
		insn->bb_false = NULL;
		kill_use(&insn->cond);
		insn->cond = NULL;
		return REPEAT_CSE;
	}

	/* Conditional on a SETNE $0 or SETEQ $0 */
	if (cond->type == PSEUDO_REG) {
		struct instruction *def = cond->def;

		if (def->opcode == OP_SET_NE || def->opcode == OP_SET_EQ) {
			if (constant(def->src1) && !def->src1->value)
				return simplify_cond_branch(insn, cond, def, &def->src2);
			if (constant(def->src2) && !def->src2->value)
				return simplify_cond_branch(insn, cond, def, &def->src1);
		}
		if (def->opcode == OP_SEL) {
			if (constant(def->src2) && constant(def->src3)) {
				long long val1 = def->src2->value;
				long long val2 = def->src3->value;
				if (!val1 && !val2) {
					insert_branch(insn->bb, insn, insn->bb_false);
					return REPEAT_CSE;
				}
				if (val1 && val2) {
					insert_branch(insn->bb, insn, insn->bb_true);
					return REPEAT_CSE;
				}
				if (val2) {
					struct basic_block *true = insn->bb_true;
					struct basic_block *false = insn->bb_false;
					insn->bb_false = true;
					insn->bb_true = false;
				}
				use_pseudo(def->src1, &insn->cond);
				remove_usage(cond, &insn->cond);
				return REPEAT_CSE;
			}
		}
	}
	return 0;
}

static int simplify_switch(struct instruction *insn)
{
	pseudo_t cond = insn->cond;
	long long val;
	struct multijmp *jmp;

	if (!constant(cond))
		return 0;
	val = insn->cond->value;

	FOR_EACH_PTR(insn->multijmp_list, jmp) {
		/* Default case */
		if (jmp->begin > jmp->end)
			goto found;
		if (val >= jmp->begin && val <= jmp->end)
			goto found;
	} END_FOR_EACH_PTR(jmp);
	warning(insn->bb->pos, "Impossible case statement");
	return 0;

found:
	insert_branch(insn->bb, insn, jmp->target);
	return REPEAT_CSE;
}

int simplify_instruction(struct instruction *insn)
{
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
		if (dead_insn(insn, NULL, NULL, NULL))
			return REPEAT_CSE | REPEAT_SYMBOL_CLEANUP;
		return replace_with_pseudo(insn, insn->symbol);
	case OP_PTRCAST:
	case OP_CAST:
		return simplify_cast(insn);
	case OP_PHI:
		if (dead_insn(insn, NULL, NULL, NULL)) {
			clear_phi(insn);
			return REPEAT_CSE;
		}
		return clean_up_phi(insn);
	case OP_PHISOURCE:
		if (dead_insn(insn, &insn->phi_src, NULL, NULL))
			return REPEAT_CSE;
		break;
	case OP_SEL:
		return simplify_select(insn);
	case OP_BR:
		return simplify_branch(insn);
	case OP_SWITCH:
		return simplify_switch(insn);
	}
	return 0;
}
