/*
 * Linearize - walk the statement tree (but _not_ the expressions)
 * to generate a linear version of it and the basic blocks. 
 *
 * NOTE! We're not interested in the actual sub-expressions yet,
 * even though they can generate conditional branches and
 * subroutine calls. That's all "local" behaviour.
 *
 * Copyright (C) 2004 Linus Torvalds
 * Copyright (C) 2004 Christopher Li
 */

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt);
pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr);

static pseudo_t add_binary_op(struct entrypoint *ep, struct expression *expr, int op, pseudo_t left, pseudo_t right);
static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val);
static pseudo_t add_const_value(struct entrypoint *ep, struct position pos, struct symbol *ctype, int val);
static pseudo_t add_load(struct entrypoint *ep, struct expression *expr, pseudo_t addr);


struct pseudo void_pseudo = {};

static struct instruction *alloc_instruction(int opcode, struct symbol *type)
{
	struct instruction * insn = __alloc_instruction(0);
	insn->type = type;
	insn->opcode = opcode;
	return insn;
}

static struct entrypoint *alloc_entrypoint(void)
{
	return __alloc_entrypoint(0);
}

static struct basic_block *alloc_basic_block(void)
{
	return __alloc_basic_block(0);
}

static struct multijmp* alloc_multijmp(struct basic_block *target, int begin, int end)
{
	struct multijmp *multijmp = __alloc_multijmp(0);
	multijmp->target = target;
	multijmp->begin = begin;
	multijmp->end = end;
	return multijmp;
}

static struct phi* alloc_phi(struct basic_block *source, pseudo_t pseudo)
{
	struct phi *phi = __alloc_phi(0);
	phi->source = source;
	phi->pseudo = pseudo;
	return phi;
}

static void show_instruction(struct instruction *insn)
{
	int op = insn->opcode;

	switch (op) {
	case OP_BADOP:
		printf("\tAIEEE! (%d %d)\n", insn->target->nr, insn->src->nr);
		break;
	case OP_RET:
		if (insn->type && insn->type != &void_ctype)
			printf("\tret %%r%d\n", insn->src->nr);
		else
			printf("\tret\n");
		break;
	case OP_BR:
		if (insn->bb_true && insn->bb_false) {
			printf("\tbr\t%%r%d, .L%p, .L%p\n", insn->cond->nr, insn->bb_true, insn->bb_false);
			break;
		}
		printf("\tbr\t.L%p\n", insn->bb_true ? insn->bb_true : insn->bb_false);
		break;

	case OP_SETVAL: {
		struct expression *expr = insn->val;
		switch (expr->type) {
		case EXPR_VALUE:
			printf("\t%%r%d <- %lld\n",
				insn->target->nr, expr->value);
			break;
		case EXPR_FVALUE:
			printf("\t%%r%d <- %Lf\n",
				insn->target->nr, expr->fvalue);
			break;
		case EXPR_STRING:
			printf("\t%%r%d <- %s\n",
				insn->target->nr, show_string(expr->string));
			break;
		case EXPR_SYMBOL:
			printf("\t%%r%d <- %s\n",  
				insn->target->nr, show_ident(expr->symbol->ident));
			break;
		default:
			printf("\t SETVAL ?? ");
		}
		break;
	}
	case OP_SWITCH: {
		struct multijmp *jmp;
		printf("\tswitch %%r%d", insn->cond->nr);
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			if (jmp->begin == jmp->end)
				printf(", %d -> .L%p", jmp->begin, jmp->target);
			else if (jmp->begin < jmp->end)
				printf(", %d ... %d -> .L%p", jmp->begin, jmp->end, jmp->target);
			else
				printf(", default -> .L%p\n", jmp->target);
		} END_FOR_EACH_PTR;
		printf("\n");
		break;
	}
	
	case OP_PHI: {
		struct phi *phi;
		char *s = " ";
		printf("\t%%r%d <- phi", insn->target->nr);
		FOR_EACH_PTR(insn->phi_list, phi) {
			printf("%s(%%r%d, .L%p)", s, phi->pseudo->nr, phi->source);
			s = ", ";
		} END_FOR_EACH_PTR;
		printf("\n");
		break;
	}	
	case OP_LOAD:
		printf("\tload %%r%d <- [%%r%d]\n", insn->target->nr, insn->src->nr);
		break;
	case OP_STORE:
		printf("\tstore %%r%d -> [%%r%d]\n", insn->target->nr, insn->src->nr);
		break;
	case OP_CALL: {
		struct pseudo *arg;
		printf("\t%%r%d <- CALL %%r%d", insn->target->nr, insn->func->nr);
		FOR_EACH_PTR(insn->arguments, arg) {
			printf(", %%r%d", arg->nr);
		} END_FOR_EACH_PTR;
		printf("\n");
		break;
	}
	case OP_CAST:
		printf("\t%%r%d <- CAST(%d->%d) %%r%d\n",
			insn->target->nr,
			insn->orig_type->bit_size, insn->type->bit_size, 
			insn->src->nr);
		break;
	case OP_BINARY ... OP_BINARY_END:
	case OP_LOGICAL ... OP_LOGICAL_END: {
		static const char *opname[] = {
			[OP_ADD - OP_BINARY] = "add", [OP_SUB - OP_BINARY] = "sub",
			[OP_MUL - OP_BINARY] = "mul", [OP_DIV - OP_BINARY] = "div",
			[OP_MOD - OP_BINARY] = "mod", [OP_AND - OP_BINARY] = "and",
			[OP_OR  - OP_BINARY] = "or",  [OP_XOR - OP_BINARY] = "xor",
			[OP_SHL - OP_BINARY] = "shl", [OP_SHR - OP_BINARY] = "shr",
		};
		printf("\t%%r%d <- %s  %%r%d, %%r%d\n",
			insn->target->nr,
			opname[op - OP_BINARY], insn->src1->nr, insn->src2->nr);
		break;
	}

	case OP_BINCMP ... OP_BINCMP_END: {
		static const char *opname[] = {
			[OP_SET_EQ - OP_BINCMP] = "seteq",
			[OP_SET_NE - OP_BINCMP] = "setne",
			[OP_SET_LE - OP_BINCMP] = "setle",
			[OP_SET_GE - OP_BINCMP] = "setge",
			[OP_SET_LT - OP_BINCMP] = "setlt",
			[OP_SET_GT - OP_BINCMP] = "setgt",
			[OP_SET_BE - OP_BINCMP] = "setbe",
			[OP_SET_AE - OP_BINCMP] = "setae",
			[OP_SET_A - OP_BINCMP] = "seta",
			[OP_SET_B - OP_BINCMP] = "setb",
		};
		printf("\t%%r%d <- %s  %%r%d, %%r%d\n",
			insn->target->nr,
			opname[op - OP_BINCMP], insn->src1->nr, insn->src2->nr);
		break;
	}

	case OP_NOT: case OP_NEG:
		printf("\t%%r%d <- %s %%r%d\n",
			insn->target->nr,
			op == OP_NOT ? "not" : "neg", insn->src1->nr);
		break;
	default:
		printf("\top %d ???\n", op);
	}
}

static void show_bb(struct basic_block *bb)
{
	struct instruction *insn;

	printf("bb: %p\n", bb);
	if (bb->parents) {
		struct basic_block *from;
		FOR_EACH_PTR(bb->parents, from) {
			printf("  **from %p**\n", from);
		} END_FOR_EACH_PTR;
	}
	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR;
	if (!bb_terminated(bb))
		printf("\tEND\n");
	printf("\n");
}

void show_entry(struct entrypoint *ep)
{
	struct symbol *sym;
	struct basic_block *bb;

	printf("ep %p: %s\n", ep, show_ident(ep->name->ident));

	FOR_EACH_PTR(ep->syms, sym) {
		printf("   sym: %p %s\n", sym, show_ident(sym->ident));
	} END_FOR_EACH_PTR;

	printf("\n");

	FOR_EACH_PTR(ep->bbs, bb) {
		show_bb(bb);
	} END_FOR_EACH_PTR;

	printf("\n");
}

static void bind_label(struct symbol *label, struct basic_block *bb, struct position pos)
{
	if (label->bb_target)
		warn(pos, "label already bound");
	label->bb_target = bb;
}

static struct basic_block * get_bound_block(struct entrypoint *ep, struct symbol *label)
{
	struct basic_block *bb = label->bb_target;

	if (!bb) {
		label->bb_target = bb = alloc_basic_block();
		bb->flags |= BB_REACHABLE;
	}
	return bb;
}

static void add_goto(struct entrypoint *ep, struct basic_block *dst)
{
	struct basic_block *src = ep->active;
	if (bb_reachable(src)) {
		struct instruction *br = alloc_instruction(OP_BR, NULL);
		br->bb_true = dst;
		add_bb(&dst->parents, src);
		add_instruction(&src->insns, br);
		ep->active = NULL;
	}
}

static void add_one_insn(struct entrypoint *ep, struct position pos, struct instruction *insn)
{
	struct basic_block *bb = ep->active;    

	if (bb_reachable(bb))
		add_instruction(&bb->insns, insn);
}

static void set_activeblock(struct entrypoint *ep, struct basic_block *bb)
{
	if (!bb_terminated(ep->active))
		add_goto(ep, bb);

	ep->active = bb;
	if (bb_reachable(bb))
		add_bb(&ep->bbs, bb);
}

static void add_branch(struct entrypoint *ep, struct expression *expr, pseudo_t cond, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *bb = ep->active;
	struct instruction *br;

	if (bb_reachable(bb)) {
       		br = alloc_instruction(OP_BR, expr->ctype);
		br->cond = cond;
		br->bb_true = bb_true;
		br->bb_false = bb_false;
		add_bb(&bb_true->parents, bb);
		add_bb(&bb_false->parents, bb);
		add_one_insn(ep, expr->pos, br);
	}
}

/* Dummy pseudo allocator */
static pseudo_t alloc_pseudo(void)
{
	static int nr = 0;
	struct pseudo * pseudo = __alloc_pseudo(0);
	pseudo->nr = ++nr;
	return pseudo;
}

/*
 * FIXME! Not all accesses are memory loads. We should
 * check what kind of symbol is behind the dereference.
 */
static pseudo_t linearize_address_gen(struct entrypoint *ep, struct expression *expr)
{
	if (expr->type == EXPR_PREOP)
		return linearize_expression(ep, expr->unop);
	if (expr->type == EXPR_BITFIELD)
		return linearize_expression(ep, expr->address);
	warn(expr->pos, "generating address of non-lvalue");
	return VOID;
}

static void linearize_store_gen(struct entrypoint *ep, pseudo_t value, struct expression *expr, pseudo_t addr)
{
	struct instruction *store = alloc_instruction(OP_STORE, expr->ctype);

	if (expr->type == EXPR_BITFIELD) {
		unsigned long mask = ((1<<expr->nrbits)-1) << expr->bitpos;
		pseudo_t andmask, ormask, shift, orig;
		if (expr->bitpos) {
			shift = add_const_value(ep, expr->pos, &uint_ctype, expr->bitpos);
			value = add_binary_op(ep, expr, OP_SHL, value, shift);
		}
		orig = add_load(ep, expr, addr);
		andmask = add_const_value(ep, expr->pos, &uint_ctype, ~mask);
		value = add_binary_op(ep, expr, OP_AND, orig, andmask);
		ormask = add_const_value(ep, expr->pos, &uint_ctype, mask);
		value = add_binary_op(ep, expr, OP_OR, orig, ormask);
	}

	store->target = value;
	store->src = addr;
	add_one_insn(ep, expr->pos, store);
}

static pseudo_t add_binary_op(struct entrypoint *ep, struct expression *expr, int op, pseudo_t left, pseudo_t right)
{
	struct instruction *insn = alloc_instruction(op, expr->ctype);
	pseudo_t target = alloc_pseudo();
	insn->target = target;
	insn->src1 = left;
	insn->src2 = right;
	add_one_insn(ep, expr->pos, insn);
	return target;
}

static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val)
{
	struct instruction *insn = alloc_instruction(OP_SETVAL, ctype);
	pseudo_t target = alloc_pseudo();
	insn->target = target;
	insn->val = val;
	add_one_insn(ep, val->pos, insn);
	return target;
}

static pseudo_t add_const_value(struct entrypoint *ep, struct position pos, struct symbol *ctype, int val)
{
	struct expression *expr = alloc_const_expression(pos, val);
	return add_setval(ep, ctype, expr);
}

static pseudo_t add_load(struct entrypoint *ep, struct expression *expr, pseudo_t addr)
{
	pseudo_t new = alloc_pseudo();
	struct instruction *insn = alloc_instruction(OP_LOAD, expr->ctype);

	insn->target = new;
	insn->src = addr;
	add_one_insn(ep, expr->pos, insn);
	return new;
}

static pseudo_t linearize_load_gen(struct entrypoint *ep, struct expression *expr, pseudo_t addr)
{
	pseudo_t new = add_load(ep, expr, addr);
	if (expr->type == EXPR_PREOP)
		return new;

	if (expr->type == EXPR_BITFIELD) {
		pseudo_t mask;
		if (expr->bitpos) {
			pseudo_t shift = add_const_value(ep, expr->pos, &uint_ctype, expr->bitpos);
			new = add_binary_op(ep, expr, OP_SHR, new, shift);
		}
		mask = add_const_value(ep, expr->pos, &uint_ctype, (1<<expr->nrbits)-1);
		return add_binary_op(ep, expr, OP_AND, new, mask);
	}

	warn(expr->pos, "loading unknown expression");
	return new;		
}

static pseudo_t linearize_access(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t addr = linearize_address_gen(ep, expr);
	return linearize_load_gen(ep, expr, addr);
}

/* FIXME: FP */
static pseudo_t linearize_inc_dec(struct entrypoint *ep, struct expression *expr, int postop)
{
	pseudo_t addr = linearize_address_gen(ep, expr->unop);
	pseudo_t old, new, one;
	int op = expr->op == SPECIAL_INCREMENT ? OP_ADD : OP_SUB;

	old = linearize_load_gen(ep, expr->unop, addr);
	one = add_const_value(ep, expr->pos, expr->ctype, 1);
	new = add_binary_op(ep, expr, op, old, one);
	linearize_store_gen(ep, new, expr->unop, addr);
	return postop ? old : new;
}

static pseudo_t add_uniop(struct entrypoint *ep, struct expression *expr, int op, pseudo_t src)
{
	pseudo_t new = alloc_pseudo();
	struct instruction *insn = alloc_instruction(op, expr->ctype);
	insn->target = new;
	insn->src1 = src;
	add_one_insn(ep, expr->pos, insn);
	return new;
}

static pseudo_t linearize_regular_preop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t pre = linearize_expression(ep, expr->unop);
	switch (expr->op) {
	case '+':
		return pre;
	case '!': {
		pseudo_t zero = add_const_value(ep, expr->pos, expr->ctype, 0);
		return add_binary_op(ep, expr, OP_SET_EQ, pre, zero);
	}
	case '~':
		return add_uniop(ep, expr, OP_NOT, pre);
	case '-':
		return add_uniop(ep, expr, OP_NEG, pre);
	}
	return VOID;
}

static pseudo_t linearize_preop(struct entrypoint *ep, struct expression *expr)
{
	/*
	 * '*' is an lvalue access, and is fundamentally different
	 * from an arithmetic operation. Maybe it should have an
	 * expression type of its own..
	 */
	if (expr->op == '*')
		return linearize_access(ep, expr);
	if (expr->op == SPECIAL_INCREMENT || expr->op == SPECIAL_DECREMENT)
		return linearize_inc_dec(ep, expr, 0);
	return linearize_regular_preop(ep, expr);
}

static pseudo_t linearize_postop(struct entrypoint *ep, struct expression *expr)
{
	return linearize_inc_dec(ep, expr, 1);
}	

static pseudo_t linearize_assignment(struct entrypoint *ep, struct expression *expr)
{
	struct expression *target = expr->left;
	pseudo_t value, address;

	value = linearize_expression(ep, expr->right);
	address = linearize_address_gen(ep, target);
	if (expr->op != '=') {
		static const int opcode[] = {
			[SPECIAL_ADD_ASSIGN - SPECIAL_BASE] = OP_ADD,
			[SPECIAL_SUB_ASSIGN - SPECIAL_BASE] = OP_SUB,
			[SPECIAL_MUL_ASSIGN - SPECIAL_BASE] = OP_MUL,
			[SPECIAL_DIV_ASSIGN - SPECIAL_BASE] = OP_DIV,
			[SPECIAL_MOD_ASSIGN - SPECIAL_BASE] = OP_MOD,
			[SPECIAL_SHL_ASSIGN - SPECIAL_BASE] = OP_SHL,
			[SPECIAL_SHR_ASSIGN - SPECIAL_BASE] = OP_SHR,
			[SPECIAL_AND_ASSIGN - SPECIAL_BASE] = OP_AND,
			[SPECIAL_OR_ASSIGN  - SPECIAL_BASE] = OP_OR,
			[SPECIAL_XOR_ASSIGN - SPECIAL_BASE] = OP_XOR 
		};
		pseudo_t left = linearize_load_gen(ep, target, address);
		value = add_binary_op(ep, expr, opcode[expr->op - SPECIAL_BASE], left, value);
	}
	linearize_store_gen(ep, value, target, address);
	return value;
}

static pseudo_t linearize_call_expression(struct entrypoint *ep, struct expression *expr)
{
	struct expression *arg, *fn;
	struct instruction *insn = alloc_instruction(OP_CALL, expr->ctype);
	pseudo_t retval;

	if (!expr->ctype) {
		warn(expr->pos, "call with no type!");
		return VOID;
	}

	FOR_EACH_PTR(expr->args, arg) {
		pseudo_t new = linearize_expression(ep, arg);
		add_pseudo(&insn->arguments, new);
	} END_FOR_EACH_PTR;

	fn = expr->fn;
	if (fn->type == EXPR_PREOP) {
		if (fn->unop->type == EXPR_SYMBOL) {
			struct symbol *sym = fn->unop->symbol;
			if (sym->ctype.base_type->type == SYM_FN)
				fn = fn->unop;
		}
	}
	insn->func = linearize_expression(ep, fn);
	insn->target = retval = alloc_pseudo();
	add_one_insn(ep, expr->pos, insn);

	return retval;
}

static pseudo_t linearize_binop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src1, src2;
	static const int opcode[] = {
		['+'] = OP_ADD, ['-'] = OP_SUB,
		['*'] = OP_MUL, ['/'] = OP_DIV,
		['%'] = OP_MOD, ['&'] = OP_AND,
		['|'] = OP_OR,  ['^'] = OP_XOR,
		[SPECIAL_LEFTSHIFT] = OP_SHL,
		[SPECIAL_RIGHTSHIFT] = OP_SHR,
	};

	src1 = linearize_expression(ep, expr->left);
	src2 = linearize_expression(ep, expr->right);
	return add_binary_op(ep, expr, opcode[expr->op], src1, src2);
}

static pseudo_t linearize_logical_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

pseudo_t linearize_cond_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

static pseudo_t linearize_conditional(struct entrypoint *ep, struct expression *expr,
				      struct expression *cond, struct expression *expr_true,
				      struct expression *expr_false)
{
	pseudo_t src1, src2, target;
	struct basic_block *bb_true = alloc_basic_block();
	struct basic_block *bb_false = alloc_basic_block();
	struct basic_block *merge = alloc_basic_block();

	if (expr_true) {
		linearize_cond_branch(ep, cond, bb_true, bb_false);

		set_activeblock(ep, bb_true);
		src1 = linearize_expression(ep, expr_true);
		bb_true = ep->active;
		add_goto(ep, merge); 
	} else {
		src1 = linearize_expression(ep, cond);
		add_branch(ep, expr, src1, merge, bb_false);
	}

	set_activeblock(ep, bb_false);
	src2 = linearize_expression(ep, expr_false);
	bb_false = ep->active;
	set_activeblock(ep, merge);

	if (src1 != VOID && src2 != VOID) {
		struct instruction *phi_node = alloc_instruction(OP_PHI, expr->ctype);
		add_phi(&phi_node->phi_list, alloc_phi(bb_true, src1));
		add_phi(&phi_node->phi_list, alloc_phi(bb_false, src2));
		phi_node->target = target = alloc_pseudo();
		add_one_insn(ep, expr->pos, phi_node);
		set_activeblock(ep, alloc_basic_block());
		return target;
	}

	return src1 != VOID ? src1 : src2;
}

static pseudo_t linearize_logical(struct entrypoint *ep, struct expression *expr)
{
	struct expression *shortcut;

	shortcut = alloc_const_expression(expr->pos, expr->op == SPECIAL_LOGICAL_OR);
	shortcut->ctype = expr->ctype;
	return  linearize_conditional(ep, expr, expr->left, shortcut, expr->right);
}

static pseudo_t linearize_compare(struct entrypoint *ep, struct expression *expr)
{
	static const int cmpop[] = {
		['>'] = OP_SET_GT, ['<'] = OP_SET_LT,
		[SPECIAL_EQUAL] = OP_SET_EQ,
		[SPECIAL_NOTEQUAL] = OP_SET_NE,
		[SPECIAL_GTE] = OP_SET_GE,
		[SPECIAL_LTE] = OP_SET_LE,
		[SPECIAL_UNSIGNED_LT] = OP_SET_B,
		[SPECIAL_UNSIGNED_GT] = OP_SET_A,
		[SPECIAL_UNSIGNED_LTE] = OP_SET_BE,
		[SPECIAL_UNSIGNED_GTE] = OP_SET_AE,
	};

	pseudo_t src1 = linearize_expression(ep, expr->left);
	pseudo_t src2 = linearize_expression(ep, expr->right);
	return add_binary_op(ep, expr, cmpop[expr->op], src1, src2);
}


pseudo_t linearize_cond_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false)
{
	pseudo_t cond;

	if (!expr || !bb_reachable(ep->active))
		return VOID;

	switch (expr->type) {

	case EXPR_STRING:
	case EXPR_VALUE:
		add_goto(ep, expr->value ? bb_true : bb_false);
		return VOID;

	case EXPR_FVALUE:
		add_goto(ep, expr->fvalue ? bb_true : bb_false);
		return VOID;
		
	case EXPR_LOGICAL:
		linearize_logical_branch(ep, expr, bb_true, bb_false);
		return VOID;

	case EXPR_COMPARE:
		cond = linearize_compare(ep, expr);
		add_branch(ep, expr, cond, bb_true, bb_false);
		break;
		
	case EXPR_PREOP:
		if (expr->op == '!')
			return linearize_cond_branch(ep, expr->unop, bb_false, bb_true);
		/* fall through */
	default: {
		cond = linearize_expression(ep, expr);
		add_branch(ep, expr, cond, bb_true, bb_false);

		return VOID;
	}
	}
	return VOID;
}


	
static pseudo_t linearize_logical_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *next = alloc_basic_block();

	if (expr->op == SPECIAL_LOGICAL_OR)
		linearize_cond_branch(ep, expr->left, bb_true, next);
	else
		linearize_cond_branch(ep, expr->left, next, bb_false);
	set_activeblock(ep, next);
	linearize_cond_branch(ep, expr->right, bb_true, bb_false);
	return VOID;
}

pseudo_t linearize_cast(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src, result;
	struct instruction *insn;

	src = linearize_expression(ep, expr->cast_expression);
	if (src == VOID)
		return VOID;
	insn = alloc_instruction(OP_CAST, expr->ctype);
	result = alloc_pseudo();
	insn->target = result;
	insn->src = src;
	insn->orig_type = expr->cast_expression->ctype;
	add_one_insn(ep, expr->pos, insn);
	return result;
}

pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr)
{
	if (!expr)
		return VOID;

	switch (expr->type) {
	case EXPR_VALUE: case EXPR_STRING: case EXPR_SYMBOL: case EXPR_FVALUE: case EXPR_LABEL:
		return add_setval(ep, expr->ctype, expr);

	case EXPR_STATEMENT:
		return linearize_statement(ep, expr->statement);

	case EXPR_CALL:
		return linearize_call_expression(ep, expr);

	case EXPR_BINOP:
		return linearize_binop(ep, expr);

	case EXPR_LOGICAL:
		return linearize_logical(ep, expr);

	case EXPR_COMPARE:
		return  linearize_compare(ep, expr);

	case EXPR_CONDITIONAL:
		return  linearize_conditional(ep, expr, expr->conditional,
					      expr->cond_true, expr->cond_false);

	case EXPR_COMMA: {
		linearize_expression(ep, expr->left);
		return linearize_expression(ep, expr->right);
	}

	case EXPR_ASSIGNMENT:
		return linearize_assignment(ep, expr);

	case EXPR_PREOP:
		return linearize_preop(ep, expr);

	case EXPR_POSTOP:
		return linearize_postop(ep, expr);

	case EXPR_CAST:
		return linearize_cast(ep, expr);
	
	case EXPR_BITFIELD:
		return linearize_access(ep, expr);

	default: 
		warn(expr->pos, "unknown expression (%d %d)", expr->type, expr->op);
		return VOID;
	}
	return VOID;
}

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt)
{
	if (!stmt)
		return VOID;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_EXPRESSION:
		return linearize_expression(ep, stmt->expression);

	case STMT_ASM:
		/* FIXME */
		break;

	case STMT_RETURN: {
		struct expression *expr = stmt->expression;
		struct basic_block *bb_return = stmt->ret_target->bb_target;
		struct basic_block *active;
		pseudo_t src = linearize_expression(ep, expr);
		active = ep->active;
		add_goto(ep, bb_return);
		if (src != &void_pseudo) {
			struct instruction *phi_node = first_instruction(bb_return->insns);
			if (!phi_node) {
				phi_node = alloc_instruction(OP_PHI, expr->ctype);
				phi_node->target = alloc_pseudo();
				add_instruction(&bb_return->insns, phi_node);
			}
			add_phi(&phi_node->phi_list, alloc_phi(active, src));
		}
		return VOID;
	}

	case STMT_CASE: {
		struct basic_block *bb = get_bound_block(ep, stmt->case_label);
		set_activeblock(ep, bb);
		linearize_statement(ep, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		struct symbol *label = stmt->label_identifier;
		struct basic_block *bb;

		if (label->used) {
			bb = get_bound_block(ep, stmt->label_identifier);
			set_activeblock(ep, bb);
			linearize_statement(ep, stmt->label_statement);
		}
		break;
	}

	case STMT_GOTO: {
		add_goto(ep, get_bound_block(ep, stmt->goto_label));
		break;
	}

	case STMT_COMPOUND: {
		pseudo_t pseudo = NULL;
		struct statement *s;
		struct symbol *ret = stmt->ret;
		concat_symbol_list(stmt->syms, &ep->syms);
		if (ret)
			ret->bb_target = alloc_basic_block();
		FOR_EACH_PTR(stmt->stmts, s) {
			pseudo = linearize_statement(ep, s);
		} END_FOR_EACH_PTR;
		if (ret) {
			struct basic_block *bb = ret->bb_target;
			struct instruction *phi = first_instruction(bb->insns);

			if (!phi)
				return pseudo;

			set_activeblock(ep, bb);
			if (phi_list_size(phi->phi_list)==1) {
				pseudo = first_phi(phi->phi_list)->pseudo;
				delete_last_instruction(&bb->insns);
				return pseudo;
			}
			return phi->target;
		}
		return pseudo;
	}

	/*
	 * This could take 'likely/unlikely' into account, and
	 * switch the arms around appropriately..
	 */
	case STMT_IF: {
		struct basic_block *bb_true, *bb_false, *endif;
 		struct expression *cond = stmt->if_conditional;

		bb_true = alloc_basic_block();
		bb_false = endif = alloc_basic_block();

 		linearize_cond_branch(ep, cond, bb_true, bb_false);

		set_activeblock(ep, bb_true);
 		linearize_statement(ep, stmt->if_true);
 
 		if (stmt->if_false) {
			endif = alloc_basic_block();
			add_goto(ep, endif);
			set_activeblock(ep, bb_false);
 			linearize_statement(ep, stmt->if_false);
		}
		set_activeblock(ep, endif);
		break;
	}

	case STMT_SWITCH: {
		struct symbol *sym;
		struct instruction *switch_ins;
		struct basic_block *switch_end = alloc_basic_block();
		pseudo_t pseudo;

		pseudo = linearize_expression(ep, stmt->switch_expression);
		switch_ins = alloc_instruction(OP_SWITCH, NULL);
		switch_ins->cond = pseudo;
		add_one_insn(ep, stmt->pos, switch_ins);

		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct basic_block *bb_case = get_bound_block(ep, sym);
			struct multijmp *jmp;

			if (!case_stmt->case_expression) {
			      jmp = alloc_multijmp(bb_case, 1, 0);
			} else {
				int begin, end;

				begin = end = case_stmt->case_expression->value;
				if (case_stmt->case_to)
					end = case_stmt->case_to->value;
				if (begin > end)
					jmp = alloc_multijmp(bb_case, end, begin);
				else
					jmp = alloc_multijmp(bb_case, begin, end);

			}
			add_multijmp(&switch_ins->multijmp_list, jmp);
			add_bb(&bb_case->parents, ep->active);
		} END_FOR_EACH_PTR;

		bind_label(stmt->switch_break, switch_end, stmt->pos);

		/* And linearize the actual statement */
		linearize_statement(ep, stmt->switch_statement);
		set_activeblock(ep, switch_end);

		break;
	}

	case STMT_ITERATOR: {
		struct statement  *pre_statement = stmt->iterator_pre_statement;
		struct expression *pre_condition = stmt->iterator_pre_condition;
		struct statement  *statement = stmt->iterator_statement;
		struct statement  *post_statement = stmt->iterator_post_statement;
		struct expression *post_condition = stmt->iterator_post_condition;
		struct basic_block *loop_top, *loop_body, *loop_continue, *loop_end;

		concat_symbol_list(stmt->iterator_syms, &ep->syms);
		linearize_statement(ep, pre_statement);

 		loop_body = loop_top = alloc_basic_block();
 		loop_continue = alloc_basic_block();
 		loop_end = alloc_basic_block();
 
		if (!post_statement && (pre_condition == post_condition)) {
			/*
			 * If it is a while loop, optimize away the post_condition.
			 */
			post_condition = NULL;
			loop_body = loop_continue;
			loop_continue = loop_top;
			loop_top->flags |= BB_REACHABLE;
			set_activeblock(ep, loop_top);
		}

		loop_top->flags |= BB_REACHABLE;
		if (pre_condition) 
 			linearize_cond_branch(ep, pre_condition, loop_body, loop_end);

		bind_label(stmt->iterator_continue, loop_continue, stmt->pos);
		bind_label(stmt->iterator_break, loop_end, stmt->pos);

		set_activeblock(ep, loop_body);
		linearize_statement(ep, statement);
		add_goto(ep, loop_continue);

		if (post_condition) {
			set_activeblock(ep, loop_continue);
			linearize_statement(ep, post_statement);
 			linearize_cond_branch(ep, post_condition, loop_top, loop_end);
		}

		set_activeblock(ep, loop_end);
		break;
	}

	default:
		break;
	}
	return VOID;
}

void mark_bb_reachable(struct basic_block *bb)
{
	struct basic_block *child;
	struct terminator_iterator term;
	struct basic_block_list *bbstack = NULL;

	if (!bb || bb->flags & BB_REACHABLE)
		return;

	add_bb(&bbstack, bb);
	while (bbstack) {
		bb = delete_last_basic_block(&bbstack);
		if (bb->flags & BB_REACHABLE)
			continue;
		bb->flags |= BB_REACHABLE;
		init_terminator_iterator(last_instruction(bb->insns), &term);
		while ((child=next_terminator_bb(&term)) != NULL) {
			if (!(child->flags & BB_REACHABLE))
				add_bb(&bbstack, child);
		}
	}
}

void remove_unreachable_bbs(struct basic_block_list **bblist)
{
	struct basic_block *bb, *child;
	struct list_iterator iterator;
	struct terminator_iterator term;

	init_iterator((struct ptr_list **) bblist, &iterator, 0);
	while((bb=next_basic_block(&iterator)) != NULL)
		bb->flags &= ~BB_REACHABLE;

	init_iterator((struct ptr_list **) bblist, &iterator, 0);
	mark_bb_reachable(next_basic_block(&iterator));
	while((bb=next_basic_block(&iterator)) != NULL) {
		if (bb->flags & BB_REACHABLE)
			continue;
		init_terminator_iterator(last_instruction(bb->insns), &term);
		while ((child=next_terminator_bb(&term)) != NULL)
			replace_basic_block_list(&child->parents, bb, NULL);
		delete_iterator(&iterator);
	}
}

void pack_basic_blocks(struct basic_block_list **bblist)
{
	struct basic_block *bb;
	struct list_iterator iterator;

	remove_unreachable_bbs(bblist);
	init_bb_iterator(bblist, &iterator, 0);
	while((bb=next_basic_block(&iterator)) != NULL) {
		struct list_iterator it_parents;
		struct terminator_iterator term;
		struct instruction *jmp;
		struct basic_block *target, *sibling, *parent;

		if (!is_branch_goto(jmp=last_instruction(bb->insns)))
			continue;

		target = jmp->bb_true ? jmp->bb_true : jmp->bb_false;
		if (target == bb)
			continue;
		if (bb_list_size(target->parents) != 1 && jmp != first_instruction(bb->insns))
			continue;

		/* Transfer the parents' terminator to target directly. */
		replace_basic_block_list(&target->parents, bb, NULL);
		init_bb_iterator(&bb->parents, &it_parents, 0);
		while((parent=next_basic_block(&it_parents)) != NULL) {
			init_terminator_iterator(last_instruction(parent->insns), &term);
			while ((sibling=next_terminator_bb(&term)) != NULL) {
				if (sibling == bb) {
					replace_terminator_bb(&term, target);
					add_bb(&target->parents, parent);
				}
			}
		}

		/* Move the instructions to the target block. */
		delete_last_instruction(&bb->insns);
		if (bb->insns) {
			concat_instruction_list(target->insns, &bb->insns);
			free_instruction_list(&target->insns);
			target->insns = bb->insns;
		} 
		delete_iterator(&iterator);
	}
}

struct entrypoint *linearize_symbol(struct symbol *sym)
{
	struct symbol *base_type;
	struct entrypoint *ret_ep = NULL;

	if (!sym)
		return NULL;
	base_type = sym->ctype.base_type;
	if (!base_type)
		return NULL;
	if (base_type->type == SYM_FN) {
		if (base_type->stmt) {
			struct entrypoint *ep = alloc_entrypoint();
			struct basic_block *bb = alloc_basic_block();
			pseudo_t result;

			ep->name = sym;
			bb->flags |= BB_REACHABLE;
			set_activeblock(ep, bb);
			concat_symbol_list(base_type->arguments, &ep->syms);
			result = linearize_statement(ep, base_type->stmt);
			if (bb_reachable(ep->active) && !bb_terminated(ep->active)) {
				struct symbol *ret_type = base_type->ctype.base_type;
				struct instruction *insn = alloc_instruction(OP_RET, ret_type);
				struct position pos = base_type->stmt->pos;
				
				insn->src = result;
				add_one_insn(ep, pos, insn);
			}
			pack_basic_blocks(&ep->bbs);
			ret_ep = ep;
		}
	}

	return ret_ep;
}
