// SPDX-License-Identifier: MIT
// Copyright (C) 2021 Luc Van Oostenryck

///
// Symbolic checker for Sparse's IR
// --------------------------------
//
// This is an example client program with a dual purpose:
//	# It shows how to translate Sparse's IR into the language
//	  of SMT solvers (only the part concerning integers,
//	  floating-point and memory is ignored).
//	# It's used as a simple symbolic checker for the IR.
//	  The idea is to create a mini-language that allows to
//	  express some assertions with some pre-conditions.

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include <boolector.h>
#include "lib.h"
#include "expression.h"
#include "linearize.h"
#include "symbol.h"
#include "builtin.h"


#define dyntype incomplete_ctype
static const struct builtin_fn builtins_scheck[] = {
	{ "__assume", &void_ctype, 0, { &dyntype }, .op = &generic_int_op },
	{ "__assert", &void_ctype, 0, { &bool_ctype }, .op = &generic_int_op },
	{ "__assert_eq", &void_ctype, 0, { &dyntype, &dyntype }, .op = &generic_int_op },
	{ "__assert_const", &void_ctype, 0, { &dyntype, &dyntype }, .op = &generic_int_op },
	{}
};


static BoolectorSort get_sort(Btor *btor, struct symbol *type, struct position pos)
{
	if (!is_int_type(type)) {
		sparse_error(pos, "invalid type");
		return NULL;
	}
	return boolector_bitvec_sort(btor, type->bit_size);
}

static BoolectorNode *mkvar(Btor *btor, BoolectorSort s, pseudo_t pseudo)
{
	static char buff[33];
	BoolectorNode *n;

	switch (pseudo->type) {
	case PSEUDO_VAL:
		sprintf(buff, "%llx", pseudo->value);
		return boolector_consth(btor, s, buff);
	case PSEUDO_ARG:
	case PSEUDO_REG:
		if (pseudo->priv)
			return pseudo->priv;
		n = boolector_var(btor, s, show_pseudo(pseudo));
		break;
	default:
		fprintf(stderr, "invalid pseudo: %s\n", show_pseudo(pseudo));
		return NULL;
	}
	return pseudo->priv = n;
}

static BoolectorNode *mktvar(Btor *btor, struct instruction *insn, pseudo_t src)
{
	BoolectorSort s = get_sort(btor, insn->type, insn->pos);
	return mkvar(btor, s, src);
}

static BoolectorNode *mkivar(Btor *btor, struct instruction *insn, pseudo_t src)
{
	BoolectorSort s = get_sort(btor, insn->itype, insn->pos);
	return mkvar(btor, s, src);
}

static BoolectorNode *get_arg(Btor *btor, struct instruction *insn, int idx)
{
	pseudo_t arg = ptr_list_nth(insn->arguments, idx);
	struct symbol *type = ptr_list_nth(insn->fntypes, idx + 1);
	BoolectorSort s = get_sort(btor, type, insn->pos);

	return mkvar(btor, s, arg);
}

static BoolectorNode *zext(Btor *btor, struct instruction *insn, BoolectorNode *s)
{
	int old = boolector_get_width(btor, s);
	int new = insn->type->bit_size;
	return boolector_uext(btor, s, new - old);
}

static BoolectorNode *sext(Btor *btor, struct instruction *insn, BoolectorNode *s)
{
	int old = boolector_get_width(btor, s);
	int new = insn->type->bit_size;
	return boolector_sext(btor, s, new - old);
}

static BoolectorNode *slice(Btor *btor, struct instruction *insn, BoolectorNode *s)
{
	int old = boolector_get_width(btor, s);
	int new = insn->type->bit_size;
	return boolector_slice(btor, s, old - new - 1, 0);
}

static void binary(Btor *btor, BoolectorSort s, struct instruction *insn)
{
	BoolectorNode *t, *a, *b;

	a = mkvar(btor, s, insn->src1);
	b = mkvar(btor, s, insn->src2);
	if (!a || !b)
		return;
	switch (insn->opcode) {
	case OP_ADD:	t = boolector_add(btor, a, b); break;
	case OP_SUB:	t = boolector_sub(btor, a, b); break;
	case OP_MUL:	t = boolector_mul(btor, a, b); break;
	case OP_AND:	t = boolector_and(btor, a, b); break;
	case OP_OR:	t = boolector_or (btor, a, b); break;
	case OP_XOR:	t = boolector_xor(btor, a, b); break;
	case OP_SHL:	t = boolector_sll(btor, a, b); break;
	case OP_LSR:	t = boolector_srl(btor, a, b); break;
	case OP_ASR:	t = boolector_sra(btor, a, b); break;
	case OP_DIVS:	t = boolector_sdiv(btor, a, b); break;
	case OP_DIVU:	t = boolector_udiv(btor, a, b); break;
	case OP_MODS:	t = boolector_srem(btor, a, b); break;
	case OP_MODU:	t = boolector_urem(btor, a, b); break;
	case OP_SET_EQ:	t = zext(btor, insn, boolector_eq(btor, a, b)); break;
	case OP_SET_NE:	t = zext(btor, insn, boolector_ne(btor, a, b)); break;
	case OP_SET_LT:	t = zext(btor, insn, boolector_slt(btor, a, b)); break;
	case OP_SET_LE:	t = zext(btor, insn, boolector_slte(btor, a, b)); break;
	case OP_SET_GE:	t = zext(btor, insn, boolector_sgte(btor, a, b)); break;
	case OP_SET_GT:	t = zext(btor, insn, boolector_sgt(btor, a, b)); break;
	case OP_SET_B:	t = zext(btor, insn, boolector_ult(btor, a, b)); break;
	case OP_SET_BE:	t = zext(btor, insn, boolector_ulte(btor, a, b)); break;
	case OP_SET_AE:	t = zext(btor, insn, boolector_ugte(btor, a, b)); break;
	case OP_SET_A:	t = zext(btor, insn, boolector_ugt(btor, a, b)); break;
	default:
		fprintf(stderr, "unsupported insn: %s\n", show_instruction(insn));
		return;
	}
	insn->target->priv = t;
}

static void binop(Btor *btor, struct instruction *insn)
{
	BoolectorSort s = get_sort(btor, insn->type, insn->pos);
	binary(btor, s, insn);
}

static void icmp(Btor *btor, struct instruction *insn)
{
	BoolectorSort s = get_sort(btor, insn->itype, insn->pos);
	binary(btor, s, insn);
}

static void unop(Btor *btor, struct instruction *insn)
{
	pseudo_t src = insn->src;
	BoolectorNode *t;

	switch (insn->opcode) {
	case OP_SEXT:	t = sext(btor, insn, mkivar(btor, insn, src)); break;
	case OP_ZEXT:	t = zext(btor, insn, mkivar(btor, insn, src)); break;
	case OP_TRUNC:	t = slice(btor, insn, mkivar(btor, insn, src)); break;

	case OP_NEG:	t = boolector_neg(btor, mktvar(btor, insn, src)); break;
	case OP_NOT:	t = boolector_not(btor, mktvar(btor, insn, src)); break;
	default:
		fprintf(stderr, "unsupported insn: %s\n", show_instruction(insn));
		return;
	}
	insn->target->priv = t;
}

static void ternop(Btor *btor, struct instruction *insn)
{
	BoolectorSort s = get_sort(btor, insn->type, insn->pos);
	BoolectorNode *t, *a, *b, *c, *z, *d;

	a = mkvar(btor, s, insn->src1);
	b = mkvar(btor, s, insn->src2);
	c = mkvar(btor, s, insn->src3);
	if (!a || !b || !c)
		return;
	switch (insn->opcode) {
	case OP_SEL:
		z = boolector_zero(btor, s);
		d = boolector_ne(btor, a, z);
		t = boolector_cond(btor, d, b, c);
		break;
	default:
		fprintf(stderr, "unsupported insn: %s\n", show_instruction(insn));
		return;
	}
	insn->target->priv = t;
}

static bool add_precondition(Btor *btor, BoolectorNode **pre, struct instruction *insn)
{
	BoolectorNode *a = get_arg(btor, insn, 0);
	BoolectorNode *z = boolector_zero(btor, boolector_get_sort(btor, a));
	BoolectorNode *n = boolector_ne(btor, a, z);
	BoolectorNode *p = boolector_and(btor, *pre, n);
	*pre = p;
	return true;
}

static bool check_btor(Btor *btor, BoolectorNode *p, BoolectorNode *n, struct instruction *insn)
{
	char model_format[] = "btor";
	int res;

	n = boolector_implies(btor, p, n);
	boolector_assert(btor, boolector_not(btor, n));
	res = boolector_sat(btor);
	switch (res) {
	case BOOLECTOR_UNSAT:
		return 1;
	case BOOLECTOR_SAT:
		sparse_error(insn->pos, "assertion failed");
		show_entry(insn->bb->ep);
		boolector_dump_btor(btor, stdout);
		boolector_print_model(btor, model_format, stdout);
		break;
	default:
		sparse_error(insn->pos, "SMT failure");
		break;
	}
	return 0;
}

static bool check_assert(Btor *btor, BoolectorNode *pre, struct instruction *insn)
{
	BoolectorNode *a = get_arg(btor, insn, 0);
	BoolectorNode *z = boolector_zero(btor, boolector_get_sort(btor, a));
	BoolectorNode *n = boolector_ne(btor, a, z);
	return check_btor(btor, pre, n, insn);
}

static bool check_equal(Btor *btor, BoolectorNode *pre, struct instruction *insn)
{
	BoolectorNode *a = get_arg(btor, insn, 0);
	BoolectorNode *b = get_arg(btor, insn, 1);
	BoolectorNode *n = boolector_eq(btor, a, b);
	return check_btor(btor, pre, n, insn);
}

static bool check_const(Btor *ctxt, struct instruction *insn)
{
	pseudo_t src1 = ptr_list_nth(insn->arguments, 0);
	pseudo_t src2 = ptr_list_nth(insn->arguments, 1);

	if (src2->type != PSEUDO_VAL)
		sparse_error(insn->pos, "should be a constant: %s", show_pseudo(src2));
	if (src1 == src2)
		return 1;
	if (src1->type != PSEUDO_VAL)
		sparse_error(insn->pos, "not a constant: %s", show_pseudo(src1));
	else
		sparse_error(insn->pos, "invalid value: %s != %s", show_pseudo(src1), show_pseudo(src2));
	return 0;
}

static bool check_call(Btor *btor, BoolectorNode **pre, struct instruction *insn)
{
	pseudo_t func = insn->func;
	struct ident *ident = func->ident;

	if (ident == &__assume_ident)
		return add_precondition(btor, pre, insn);
	if (ident == &__assert_ident)
		return check_assert(btor, *pre, insn);
	if (ident == &__assert_eq_ident)
		return check_equal(btor, *pre, insn);
	if (ident == &__assert_const_ident)
		return check_const(btor, insn);
	return 0;
}

static bool check_function(struct entrypoint *ep)
{
	Btor *btor = boolector_new();
	BoolectorNode *pre = boolector_true(btor);
	struct basic_block *bb;
	int rc = 0;

	boolector_set_opt(btor, BTOR_OPT_MODEL_GEN, 1);
	boolector_set_opt(btor, BTOR_OPT_INCREMENTAL, 1);

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn;
		FOR_EACH_PTR(bb->insns, insn) {
			if (!insn->bb)
				continue;
			switch (insn->opcode) {
			case OP_ENTRY:
				continue;
			case OP_BINARY ... OP_BINARY_END:
				binop(btor, insn);
				break;
			case OP_BINCMP ... OP_BINCMP_END:
				icmp(btor, insn);
				break;
			case OP_UNOP ... OP_UNOP_END:
				unop(btor, insn);
				break;
			case OP_SEL:
				ternop(btor, insn);
				break;
			case OP_CALL:
				rc &= check_call(btor, &pre, insn);
				break;
			case OP_RET:
				goto out;
			case OP_INLINED_CALL:
			case OP_DEATHNOTE:
			case OP_NOP:
			case OP_CONTEXT:
				continue;
			default:
				fprintf(stderr, "unsupported insn: %s\n", show_instruction(insn));
				goto out;
			}
		} END_FOR_EACH_PTR(insn);
	} END_FOR_EACH_PTR(bb);
	fprintf(stderr, "unterminated function\n");

out:
	boolector_release_all(btor);
	boolector_delete(btor);
	return rc;
}

static void check_functions(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (!ep || !ep->entry)
			continue;
		check_function(ep);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;

	Wdecl = 0;

	sparse_initialize(argc, argv, &filelist);

	declare_builtins(0, builtins_scheck);
	predefine_strong("__SYMBOLIC_CHECKER__");

	// Expand, linearize and check.
	FOR_EACH_PTR(filelist, file) {
		check_functions(sparse(file));
	} END_FOR_EACH_PTR(file);
	return 0;
}
