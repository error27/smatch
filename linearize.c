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

static void add_setcc(struct entrypoint *ep, struct expression *expr, pseudo_t val);
static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right);
static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val);
static pseudo_t add_const_value(struct entrypoint *ep, struct position pos, struct symbol *ctype, int val);
static pseudo_t add_load(struct entrypoint *ep, struct expression *expr, pseudo_t addr);

pseudo_t linearize_initializer(struct entrypoint *ep, pseudo_t baseaddr, struct expression *initializer);

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

static struct basic_block *alloc_basic_block(struct position pos)
{
	struct basic_block *bb = __alloc_basic_block(0);
	bb->context = -1;
	bb->pos = pos;
	return bb;
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

static inline int regno(pseudo_t n)
{
	int retval = -1;
	if (n)
		retval = n->nr;
	return retval;
}

static void show_instruction(struct instruction *insn)
{
	int op = insn->opcode;

	switch (op) {
	case OP_BADOP:
		printf("\tAIEEE! (%d %d)\n", regno(insn->target), regno(insn->src));
		break;
	case OP_RET:
		if (insn->type && insn->type != &void_ctype)
			printf("\tret %%r%d\n", regno(insn->src));
		else
			printf("\tret\n");
		break;
	case OP_BR:
		if (insn->bb_true && insn->bb_false) {
			printf("\tbr\t%%r%d, .L%p, .L%p\n", regno(insn->cond), insn->bb_true, insn->bb_false);
			break;
		}
		printf("\tbr\t.L%p\n", insn->bb_true ? insn->bb_true : insn->bb_false);
		break;

	case OP_SETVAL: {
		struct expression *expr = insn->val;
		struct symbol *sym = insn->symbol;
		int target = regno(insn->target);

		if (sym) {
			if (sym->bb_target) {
				printf("\t%%r%d <- .L%p\n", target, sym->bb_target);
				break;
			}
			if (sym->ident) {
				printf("\t%%r%d <- %s\n", target, show_ident(sym->ident));
				break;
			}
			expr = sym->initializer;
			if (!expr) {
				printf("\t%%r%d <- %s\n", target, "anon symbol");
				break;
			}
		}

		if (!expr) {
			printf("\t%%r%d <- %s\n", target, "<none>");
			break;
		}
			
		switch (expr->type) {
		case EXPR_VALUE:
			printf("\t%%r%d <- %lld\n", target, expr->value);
			break;
		case EXPR_FVALUE:
			printf("\t%%r%d <- %Lf\n", target, expr->fvalue);
			break;
		case EXPR_STRING:
			printf("\t%%r%d <- %s\n", target, show_string(expr->string));
			break;
		case EXPR_SYMBOL:
			printf("\t%%r%d <- %s\n",   target, show_ident(expr->symbol->ident));
			break;
		case EXPR_LABEL:
			printf("\t%%r%d <- .L%p\n",   target, expr->symbol->bb_target);
			break;
		default:
			printf("\t%%r%d <- SETVAL EXPR TYPE %d\n", target, expr->type);
		}
		break;
	}
	case OP_SWITCH: {
		struct multijmp *jmp;
		printf("\tswitch %%r%d", regno(insn->cond));
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			if (jmp->begin == jmp->end)
				printf(", %d -> .L%p", jmp->begin, jmp->target);
			else if (jmp->begin < jmp->end)
				printf(", %d ... %d -> .L%p", jmp->begin, jmp->end, jmp->target);
			else
				printf(", default -> .L%p\n", jmp->target);
		} END_FOR_EACH_PTR(jmp);
		printf("\n");
		break;
	}
	case OP_COMPUTEDGOTO: {
		struct multijmp *jmp;
		printf("\tjmp *%%r%d", regno(insn->target));
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			printf(", .L%p", jmp->target);
		} END_FOR_EACH_PTR(jmp);
		printf("\n");
		break;
	}
	
	case OP_PHI: {
		struct phi *phi;
		const char *s = " ";
		printf("\t%%r%d <- phi", regno(insn->target));
		FOR_EACH_PTR(insn->phi_list, phi) {
			printf("%s(%%r%d, .L%p)", s, phi->pseudo->nr, phi->source);
			s = ", ";
		} END_FOR_EACH_PTR(phi);
		printf("\n");
		break;
	}	
	case OP_LOAD:
		printf("\tload %%r%d <- [%%r%d]\n", regno(insn->target), regno(insn->src));
		break;
	case OP_STORE:
		printf("\tstore %%r%d -> [%%r%d]\n", regno(insn->target), regno(insn->src));
		break;
	case OP_CALL: {
		struct pseudo *arg;
		printf("\t%%r%d <- CALL %%r%d", regno(insn->target), insn->func->nr);
		FOR_EACH_PTR(insn->arguments, arg) {
			printf(", %%r%d", arg->nr);
		} END_FOR_EACH_PTR(arg);
		printf("\n");
		break;
	}
	case OP_CAST:
		printf("\t%%r%d <- CAST(%d->%d) %%r%d\n",
			regno(insn->target),
			insn->orig_type->bit_size, insn->type->bit_size, 
			regno(insn->src));
		break;
	case OP_BINARY ... OP_BINARY_END: {
		static const char *opname[] = {
			[OP_ADD - OP_BINARY] = "add", [OP_SUB - OP_BINARY] = "sub",
			[OP_MUL - OP_BINARY] = "mul", [OP_DIV - OP_BINARY] = "div",
			[OP_MOD - OP_BINARY] = "mod", [OP_AND - OP_BINARY] = "and",
			[OP_OR  - OP_BINARY] = "or",  [OP_XOR - OP_BINARY] = "xor",
			[OP_SHL - OP_BINARY] = "shl", [OP_SHR - OP_BINARY] = "shr",
			[OP_AND_BOOL - OP_BINARY] = "and-bool",
			[OP_OR_BOOL - OP_BINARY] = "or-bool",
			[OP_SEL - OP_BINARY] = "select",
		};
		printf("\t%%r%d <- %s  %%r%d, %%r%d\n",
			regno(insn->target),
			opname[op - OP_BINARY], regno(insn->src1), regno(insn->src2));
		break;
	}

	case OP_SLICE:
		printf("\t%%r%d <- slice  %%r%d, %d, %d\n",
			regno(insn->target),
			regno(insn->base), insn->from, insn->len);
		break;

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
			regno(insn->target),
			opname[op - OP_BINCMP], regno(insn->src1), regno(insn->src2));
		break;
	}

	case OP_MOV:
		printf("\t%%r%d <- %%r%d\n",
			regno(insn->target), regno(insn->src));
		break;
	case OP_NOT: case OP_NEG:
		printf("\t%%r%d <- %s %%r%d\n",
			regno(insn->target),
			op == OP_NOT ? "not" : "neg", insn->src1->nr);
		break;
	case OP_SETCC:
		printf("\tsetcc %%r%d\n", regno(insn->src));
		break;
	case OP_CONTEXT:
		printf("\tcontext %d\n", insn->increment);
		break;
	case OP_DEAD:
		printf("\tdeathnote %%r%d\n", regno(insn->target));
		break;
	default:
		printf("\top %d ???\n", op);
	}
}

static void show_bb(struct basic_block *bb)
{
	struct instruction *insn;

	printf("bb: %p\n", bb);
	printf("   %s:%d:%d\n", input_streams[bb->pos.stream].name, bb->pos.line,bb->pos.pos);
	if (bb->parents) {
		struct basic_block *from;
		FOR_EACH_PTR(bb->parents, from) {
			printf("  **from %p (%s:%d:%d)**\n", from,
				input_streams[from->pos.stream].name, from->pos.line, from->pos.pos);
		} END_FOR_EACH_PTR(from);
	}
	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR(insn);
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
	} END_FOR_EACH_PTR(sym);

	printf("\n");

	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb == ep->entry)
			printf("ENTRY:\n");
		show_bb(bb);
	} END_FOR_EACH_PTR(bb);

	printf("\n");
}

static void bind_label(struct symbol *label, struct basic_block *bb, struct position pos)
{
	if (label->bb_target)
		warning(pos, "label '%s' already bound", show_ident(label->ident));
	label->bb_target = bb;
}

static struct basic_block * get_bound_block(struct entrypoint *ep, struct symbol *label)
{
	struct basic_block *bb = label->bb_target;

	if (!bb) {
		label->bb_target = bb = alloc_basic_block(label->pos);
		bb->flags |= BB_REACHABLE;
	}
	return bb;
}

static void finish_block(struct entrypoint *ep)
{
	struct basic_block *src = ep->active;
	if (bb_reachable(src))
		ep->active = NULL;
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

static void add_deathnote(struct entrypoint *ep, pseudo_t pseudo)
{
	struct basic_block *bb = ep->active;
	if (pseudo != VOID && bb_reachable(bb)) {
		struct instruction *dead = alloc_instruction(OP_DEAD, NULL);
		dead->target = pseudo;
		add_instruction(&bb->insns, dead);
	}
}

static void add_one_insn(struct entrypoint *ep, struct instruction *insn)
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

static void add_setcc(struct entrypoint *ep, struct expression *expr, pseudo_t val)
{
	struct basic_block *bb = ep->active;

	if (bb_reachable(bb)) {
		struct instruction *cc = alloc_instruction(OP_SETCC, &bool_ctype);
		cc->src = val;
		add_one_insn(ep, cc);
	}
}

static void add_store(struct entrypoint *ep, struct expression *expr, pseudo_t addr, pseudo_t value)
{
	struct basic_block *bb = ep->active;

	if (bb_reachable(bb)) {
		struct instruction *store = alloc_instruction(OP_STORE, expr->ctype);
		store->target = value;
		store->src = addr;
		add_one_insn(ep, store);
	}
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
		add_one_insn(ep, br);
	}
}

/* Dummy pseudo allocator */
static pseudo_t alloc_pseudo(struct instruction *def)
{
	static int nr = 0;
	struct pseudo * pseudo = __alloc_pseudo(0);
	pseudo->nr = ++nr;
	pseudo->def = def;
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
	warning(expr->pos, "generating address of non-lvalue");
	return VOID;
}

static pseudo_t linearize_store_gen(struct entrypoint *ep, pseudo_t value, struct expression *expr, pseudo_t addr)
{
	struct symbol *ctype = expr->ctype;

	if(is_bitfield_type(ctype)) {
		unsigned long mask = ((1<<ctype->bit_size)-1) << ctype->bit_offset;
		pseudo_t shifted, andmask, ormask, orig, orig_mask, value_mask, newval;

		shifted = value;
		if (ctype->bit_offset) {
			pseudo_t shift;
			shift = add_const_value(ep, expr->pos, &uint_ctype, ctype->bit_offset);
			shifted = add_binary_op(ep, ctype, OP_SHL, value, shift);
			add_deathnote(ep, shift);
		}
		orig = add_load(ep, expr, addr);
		andmask = add_const_value(ep, expr->pos, &uint_ctype, ~mask);
		orig_mask = add_binary_op(ep, ctype, OP_AND, orig, andmask);
		add_deathnote(ep, orig);
		add_deathnote(ep, andmask);
		ormask = add_const_value(ep, expr->pos, &uint_ctype, mask);
		value_mask = add_binary_op(ep, ctype, OP_AND, shifted, ormask);
		add_deathnote(ep, ormask);
		if (shifted != value)
			add_deathnote(ep, shifted);
		newval = add_binary_op(ep, ctype, OP_OR, orig_mask, value_mask);
		add_deathnote(ep, orig_mask);
		add_deathnote(ep, value_mask);
		value = newval;
	}

	add_store(ep, expr, addr, value);
	add_deathnote(ep, addr);
	return value;
}

static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right)
{
	struct instruction *insn = alloc_instruction(op, ctype);
	pseudo_t target = alloc_pseudo(insn);
	insn->target = target;
	insn->src1 = left;
	insn->src2 = right;
	add_one_insn(ep, insn);
	return target;
}

static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val)
{
	struct instruction *insn = alloc_instruction(OP_SETVAL, ctype);
	pseudo_t target = alloc_pseudo(insn);
	insn->target = target;
	insn->val = val;
	if (!val)
		insn->symbol = ctype;
	add_one_insn(ep, insn);
	return target;
}

static pseudo_t add_const_value(struct entrypoint *ep, struct position pos, struct symbol *ctype, int val)
{
	struct expression *expr = alloc_const_expression(pos, val);
	return add_setval(ep, ctype, expr);
}

static pseudo_t add_load(struct entrypoint *ep, struct expression *expr, pseudo_t addr)
{
	struct instruction *insn = alloc_instruction(OP_LOAD, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	insn->src = addr;
	add_one_insn(ep, insn);
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
			new = add_binary_op(ep, expr->ctype, OP_SHR, new, shift);
			add_deathnote(ep, shift);
		}
		mask = add_const_value(ep, expr->pos, &uint_ctype, (1<<expr->nrbits)-1);
		new = add_binary_op(ep, expr->ctype, OP_AND, new, mask);
		add_deathnote(ep, mask);
		return new;
	}

	warning(expr->pos, "loading unknown expression");
	return new;		
}

static pseudo_t linearize_access(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t addr = linearize_address_gen(ep, expr);
	pseudo_t value = linearize_load_gen(ep, expr, addr);
	add_deathnote(ep, addr);
	return value;
}

/* FIXME: FP */
static pseudo_t linearize_inc_dec(struct entrypoint *ep, struct expression *expr, int postop)
{
	pseudo_t addr = linearize_address_gen(ep, expr->unop);
	pseudo_t old, new, one;
	int op = expr->op == SPECIAL_INCREMENT ? OP_ADD : OP_SUB;

	old = linearize_load_gen(ep, expr->unop, addr);
	one = add_const_value(ep, expr->pos, expr->ctype, 1);
	new = add_binary_op(ep, expr->ctype, op, old, one);
	linearize_store_gen(ep, new, expr->unop, addr);
	if (postop) {
		add_deathnote(ep, new);
		return old;
	}
	add_deathnote(ep, old);
	return new;
}

static pseudo_t add_uniop(struct entrypoint *ep, struct expression *expr, int op, pseudo_t src)
{
	struct instruction *insn = alloc_instruction(op, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	insn->src1 = src;
	add_one_insn(ep, insn);
	add_deathnote(ep, src);
	return new;
}

static pseudo_t linearize_slice(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t pre = linearize_expression(ep, expr->base);
	struct instruction *insn = alloc_instruction(OP_SLICE, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	insn->base = pre;
	insn->from = expr->r_bitpos;
	insn->len = expr->r_nrbits;
	add_one_insn(ep, insn);
	add_deathnote(ep, pre);
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
		return add_binary_op(ep, expr->ctype, OP_SET_EQ, pre, zero);
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
	return linearize_store_gen(ep, value, target, address);
}

static pseudo_t linearize_call_expression(struct entrypoint *ep, struct expression *expr)
{
	struct expression *arg, *fn;
	struct instruction *insn = alloc_instruction(OP_CALL, expr->ctype);
	pseudo_t retval, pseudo;
	int context_diff;

	if (!expr->ctype) {
		warning(expr->pos, "call with no type!");
		return VOID;
	}

	FOR_EACH_PTR(expr->args, arg) {
		pseudo_t new = linearize_expression(ep, arg);
		add_pseudo(&insn->arguments, new);
	} END_FOR_EACH_PTR(arg);

	fn = expr->fn;

	context_diff = 0;
	if (fn->ctype) {
		int in = fn->ctype->ctype.in_context;
		int out = fn->ctype->ctype.out_context;
		if (in < 0 || out < 0)
			in = out = 0;
		context_diff = out - in;
	}

	if (fn->type == EXPR_PREOP) {
		if (fn->unop->type == EXPR_SYMBOL) {
			struct symbol *sym = fn->unop->symbol;
			if (sym->ctype.base_type->type == SYM_FN)
				fn = fn->unop;
		}
	}
	insn->func = linearize_expression(ep, fn);
	insn->target = retval = alloc_pseudo(insn);
	add_one_insn(ep, insn);

	add_deathnote(ep, insn->func);
	FOR_EACH_PTR(insn->arguments, pseudo) {
		add_deathnote(ep, pseudo);
	} END_FOR_EACH_PTR(pseudo);

	if (context_diff) {
		insn = alloc_instruction(OP_CONTEXT, &void_ctype);
		insn->increment = context_diff;
		add_one_insn(ep, insn);
	}

	return retval;
}

static pseudo_t linearize_binop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src1, src2, dst;
	static const int opcode[] = {
		['+'] = OP_ADD, ['-'] = OP_SUB,
		['*'] = OP_MUL, ['/'] = OP_DIV,
		['%'] = OP_MOD, ['&'] = OP_AND,
		['|'] = OP_OR,  ['^'] = OP_XOR,
		[SPECIAL_LEFTSHIFT] = OP_SHL,
		[SPECIAL_RIGHTSHIFT] = OP_SHR,
		[SPECIAL_LOGICAL_AND] = OP_AND_BOOL,
		[SPECIAL_LOGICAL_OR] = OP_OR_BOOL,
	};

	src1 = linearize_expression(ep, expr->left);
	src2 = linearize_expression(ep, expr->right);
	dst = add_binary_op(ep, expr->ctype, opcode[expr->op], src1, src2);
	add_deathnote(ep, src1);
	add_deathnote(ep, src2);
	return dst;
}

static pseudo_t linearize_logical_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

pseudo_t linearize_cond_branch(struct entrypoint *ep, struct expression *expr, struct basic_block *bb_true, struct basic_block *bb_false);

static pseudo_t linearize_select(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t cond, true, false, res;

	true = linearize_expression(ep, expr->cond_true);
	false = linearize_expression(ep, expr->cond_false);
	cond = linearize_expression(ep, expr->conditional);

	add_setcc(ep, expr, cond);
	if (expr->cond_true)
		add_deathnote(ep, cond);
	else
		true = cond;
	res = add_binary_op(ep, expr->ctype, OP_SEL, true, false);
	add_deathnote(ep, true);
	add_deathnote(ep, false);
	return res;
}

static pseudo_t copy_pseudo(struct entrypoint *ep, struct expression *expr, pseudo_t src)
{
	struct basic_block *bb = ep->active;

	if (bb_reachable(bb)) {
		struct instruction *new = alloc_instruction(OP_MOV, expr->ctype);
		pseudo_t dst = alloc_pseudo(src->def);
		new->target = dst;
		new->src = src;
		add_instruction(&bb->insns, new);
		return dst;
	}
	return VOID;
}

static pseudo_t add_join_conditional(struct entrypoint *ep, struct expression *expr,
				     pseudo_t src1, struct basic_block *bb1,
				     pseudo_t src2, struct basic_block *bb2)
{
	pseudo_t target;
	struct instruction *phi_node;

	if (src1 == VOID || !bb_reachable(bb1))
		return src2;
	if (src2 == VOID || !bb_reachable(bb2))
		return src1;
	phi_node = alloc_instruction(OP_PHI, expr->ctype);
	add_phi(&phi_node->phi_list, alloc_phi(bb1, src1));
	add_phi(&phi_node->phi_list, alloc_phi(bb2, src2));
	phi_node->target = target = alloc_pseudo(phi_node);
	add_one_insn(ep, phi_node);
	return target;
}	

static pseudo_t linearize_short_conditional(struct entrypoint *ep, struct expression *expr,
					    struct expression *cond,
					    struct expression *expr_false)
{
	pseudo_t tst, src1, src2;
	struct basic_block *bb_true;
	struct basic_block *bb_false = alloc_basic_block(expr_false->pos);
	struct basic_block *merge = alloc_basic_block(expr->pos);

	tst = linearize_expression(ep, cond);
	src1 = copy_pseudo(ep, expr, tst);
	bb_true = ep->active;
	add_branch(ep, expr, tst, merge, bb_false);

	set_activeblock(ep, bb_false);
	src2 = linearize_expression(ep, expr_false);
	bb_false = ep->active;
	set_activeblock(ep, merge);

	return add_join_conditional(ep, expr, src1, bb_true, src2, bb_false);
}

static pseudo_t linearize_conditional(struct entrypoint *ep, struct expression *expr,
				      struct expression *cond,
				      struct expression *expr_true,
				      struct expression *expr_false)
{
	pseudo_t src1, src2;
	struct basic_block *bb_true = alloc_basic_block(expr_true->pos);
	struct basic_block *bb_false = alloc_basic_block(expr_false->pos);
	struct basic_block *merge = alloc_basic_block(expr->pos);

	linearize_cond_branch(ep, cond, bb_true, bb_false);

	set_activeblock(ep, bb_true);
	src1 = linearize_expression(ep, expr_true);
	bb_true = ep->active;
	add_goto(ep, merge); 

	set_activeblock(ep, bb_false);
	src2 = linearize_expression(ep, expr_false);
	bb_false = ep->active;
	set_activeblock(ep, merge);

	return add_join_conditional(ep, expr, src1, bb_true, src2, bb_false);
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
	pseudo_t dst = add_binary_op(ep, expr->ctype, cmpop[expr->op], src1, src2);
	add_deathnote(ep, src1);
	add_deathnote(ep, src2);
	return dst;
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
	struct basic_block *next = alloc_basic_block(expr->pos);

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
	if (expr->ctype->bit_size < 0) {
		add_deathnote(ep, src);
		return VOID;
	}
	insn = alloc_instruction(OP_CAST, expr->ctype);
	result = alloc_pseudo(insn);
	insn->target = result;
	insn->src = src;
	insn->orig_type = expr->cast_expression->ctype;
	add_one_insn(ep, insn);
	add_deathnote(ep, src);
	return result;
}

pseudo_t linearize_position(struct entrypoint *ep, pseudo_t baseaddr, struct expression *pos)
{
	struct expression *init_expr = pos->init_expr;
	pseudo_t offset = add_const_value(ep, pos->pos, &uint_ctype, pos->init_offset);
	pseudo_t addr = add_binary_op(ep, baseaddr->def->type, OP_ADD, baseaddr, offset);
	pseudo_t value = linearize_expression(ep, init_expr);
	linearize_store_gen(ep, value, init_expr, addr);
	add_deathnote(ep, addr);
	add_deathnote(ep, value);
	return VOID;
}

pseudo_t linearize_initializer(struct entrypoint *ep, pseudo_t baseaddr, struct expression *initializer)
{
	switch (initializer->type) {
	case EXPR_INITIALIZER: {
		struct expression *expr;
		FOR_EACH_PTR(initializer->expr_list, expr) {
			linearize_initializer(ep, baseaddr, expr);
		} END_FOR_EACH_PTR(expr);
		break;
	}
	case EXPR_POS:
		linearize_position(ep, baseaddr, initializer);
		break;
	default: {
		pseudo_t value = linearize_expression(ep, initializer);
		linearize_store_gen(ep, value, initializer, baseaddr);
		add_deathnote(ep, value);
	}
	}

	return VOID;
}

pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr)
{
	if (!expr)
		return VOID;

	switch (expr->type) {
	case EXPR_SYMBOL:
		return add_setval(ep, expr->symbol, NULL);

	case EXPR_VALUE: case EXPR_STRING: case EXPR_FVALUE: case EXPR_LABEL:
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

	case EXPR_SELECT:
		return	linearize_select(ep, expr);

	case EXPR_CONDITIONAL:
		if (!expr->cond_true)
			return linearize_short_conditional(ep, expr, expr->conditional, expr->cond_false);

		return  linearize_conditional(ep, expr, expr->conditional,
					      expr->cond_true, expr->cond_false);

	case EXPR_COMMA: {
		pseudo_t left = linearize_expression(ep, expr->left);
		add_deathnote(ep, left);
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

	case EXPR_SLICE:
		return linearize_slice(ep, expr);

	case EXPR_INITIALIZER:
	case EXPR_POS:
		warning(expr->pos, "unexpected initializer expression (%d %d)", expr->type, expr->op);
		return VOID;
	default: 
		warning(expr->pos, "unknown expression (%d %d)", expr->type, expr->op);
		return VOID;
	}
	return VOID;
}

static void linearize_one_symbol(struct entrypoint *ep, struct symbol *sym)
{
	pseudo_t address;

	if (!sym->initializer)
		return;
	
	address = add_setval(ep, sym, NULL);
	linearize_initializer(ep, address, sym->initializer);
	add_deathnote(ep, address);
}

static pseudo_t linearize_compound_statement(struct entrypoint *ep, struct statement *stmt)
{
	pseudo_t pseudo;
	struct statement *s;
	struct symbol *sym;
	struct symbol *ret = stmt->ret;

	concat_symbol_list(stmt->syms, &ep->syms);

	if (ret)
		ret->bb_target = alloc_basic_block(stmt->pos);

	FOR_EACH_PTR(stmt->syms, sym) {
		linearize_one_symbol(ep, sym);
	} END_FOR_EACH_PTR(sym);

	pseudo = VOID;
	FOR_EACH_PTR(stmt->stmts, s) {
		add_deathnote(ep, pseudo);
		pseudo = linearize_statement(ep, s);
	} END_FOR_EACH_PTR(s);

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


pseudo_t linearize_internal(struct entrypoint *ep, struct statement *stmt)
{
	struct instruction *insn = alloc_instruction(OP_CONTEXT, &void_ctype);
	struct expression *expr = stmt->expression;
	int value = 0;

	if (expr->type == EXPR_VALUE)
		value = expr->value;

	insn->increment = value;
	add_one_insn(ep, insn);
	return VOID;
}

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt)
{
	if (!stmt)
		return VOID;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_INTERNAL:
		return linearize_internal(ep, stmt);

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
		if (active && src != &void_pseudo) {
			struct instruction *phi_node = first_instruction(bb_return->insns);
			if (!phi_node) {
				phi_node = alloc_instruction(OP_PHI, expr->ctype);
				phi_node->target = alloc_pseudo(phi_node);
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
		struct symbol *sym;
		struct expression *expr;
		struct instruction *goto_ins;
		pseudo_t pseudo;

		if (stmt->goto_label) {
			add_goto(ep, get_bound_block(ep, stmt->goto_label));
			break;
		}

		expr = stmt->goto_expression;
		if (!expr)
			break;

		/* This can happen as part of simplification */
		if (expr->type == EXPR_LABEL) {
			add_goto(ep, get_bound_block(ep, expr->label_symbol));
			break;
		}

		pseudo = linearize_expression(ep, expr);
		goto_ins = alloc_instruction(OP_COMPUTEDGOTO, NULL);
		add_one_insn(ep, goto_ins);
		goto_ins->target = pseudo;

		FOR_EACH_PTR(stmt->target_list, sym) {
			struct basic_block *bb_computed = get_bound_block(ep, sym);
			struct multijmp *jmp = alloc_multijmp(bb_computed, 1, 0);
			add_multijmp(&goto_ins->multijmp_list, jmp);
			add_bb(&bb_computed->parents, ep->active);
		} END_FOR_EACH_PTR(sym);

		finish_block(ep);
		break;
	}

	case STMT_COMPOUND:
		return linearize_compound_statement(ep, stmt);

	/*
	 * This could take 'likely/unlikely' into account, and
	 * switch the arms around appropriately..
	 */
	case STMT_IF: {
		struct basic_block *bb_true, *bb_false, *endif;
 		struct expression *cond = stmt->if_conditional;

		bb_true = alloc_basic_block(stmt->pos);
		bb_false = endif = alloc_basic_block(stmt->pos);

 		linearize_cond_branch(ep, cond, bb_true, bb_false);

		set_activeblock(ep, bb_true);
 		linearize_statement(ep, stmt->if_true);
 
 		if (stmt->if_false) {
			endif = alloc_basic_block(stmt->pos);
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
		struct basic_block *switch_end = alloc_basic_block(stmt->pos);
		pseudo_t pseudo;

		pseudo = linearize_expression(ep, stmt->switch_expression);
		switch_ins = alloc_instruction(OP_SWITCH, NULL);
		switch_ins->cond = pseudo;
		add_one_insn(ep, switch_ins);

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
		} END_FOR_EACH_PTR(sym);

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

 		loop_body = loop_top = alloc_basic_block(stmt->pos);
 		loop_continue = alloc_basic_block(stmt->pos);
 		loop_end = alloc_basic_block(stmt->pos);
 
		if (pre_condition == post_condition) {
			loop_top = alloc_basic_block(stmt->pos);
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
			if (pre_condition == post_condition)
				add_goto(ep, loop_top);
			else
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

void remove_unreachable_bbs(struct entrypoint *ep)
{
	struct basic_block *bb, *child;
	struct terminator_iterator term;

	FOR_EACH_PTR(ep->bbs, bb) {
		bb->flags &= ~BB_REACHABLE;
	} END_FOR_EACH_PTR(bb);
	
	mark_bb_reachable(ep->entry);
	FOR_EACH_PTR(ep->bbs, bb) {
		if (bb->flags & BB_REACHABLE)
			continue;
		init_terminator_iterator(last_instruction(bb->insns), &term);
		while ((child=next_terminator_bb(&term)) != NULL)
			replace_basic_block_list(child->parents, bb, NULL);
		DELETE_CURRENT_PTR(bb);
	}END_FOR_EACH_PTR(bb);
}

void pack_basic_blocks(struct entrypoint *ep)
{
	struct basic_block *bb;

	remove_unreachable_bbs(ep);
	FOR_EACH_PTR(ep->bbs, bb) {
		struct terminator_iterator term;
		struct instruction *jmp;
		struct basic_block *target, *sibling, *parent;
		int parents;

		if (!is_branch_goto(jmp=last_instruction(bb->insns)))
			continue;

		target = jmp->bb_true ? jmp->bb_true : jmp->bb_false;
		if (target == bb)
			continue;
		parents = bb_list_size(target->parents);
		if (target == ep->entry)
			parents++;
		if (parents != 1 && jmp != first_instruction(bb->insns))
			continue;

		/* Transfer the parents' terminator to target directly. */
		replace_basic_block_list(target->parents, bb, NULL);
		FOR_EACH_PTR(bb->parents, parent) {
			init_terminator_iterator(last_instruction(parent->insns), &term);
			while ((sibling=next_terminator_bb(&term)) != NULL) {
				if (sibling == bb) {
					replace_terminator_bb(&term, target);
					add_bb(&target->parents, parent);
				}
			}
		} END_FOR_EACH_PTR(parent);

		/* Move the instructions to the target block. */
		delete_last_instruction(&bb->insns);
		if (bb->insns) {
			concat_instruction_list(target->insns, &bb->insns);
			free_instruction_list(&target->insns);
			target->insns = bb->insns;
		}
		if (bb == ep->entry)
			ep->entry = target;
		DELETE_CURRENT_PTR(bb);
	}END_FOR_EACH_PTR(bb);
}

/*
 * Dammit, if we have a phi-node followed by a conditional
 * branch on that phi-node, we should damn well be able to
 * do something about the source. Maybe.
 */
static void rewrite_branch(struct basic_block *bb,
	struct basic_block **ptr,
	struct basic_block *old,
	struct basic_block *new)
{
	*ptr = new;
	add_bb(&new->parents, bb);
	/* 
	 * FIXME!!! We should probably also remove bb from "old->parents",
	 * but we need to be careful that bb isn't a parent some other way.
	 */
}

static void try_to_simplify_bb(struct entrypoint *ep, struct basic_block *bb,
	struct instruction *first, struct instruction *second)
{
	struct phi *phi;

	FOR_EACH_PTR(first->phi_list, phi) {
		struct basic_block *source = phi->source;
		pseudo_t pseudo = phi->pseudo;
		struct instruction *br, *insn;

		br = last_instruction(source->insns);
		if (!br)
			continue;
		if (br->opcode != OP_BR)
			continue;

		insn = pseudo->def;
		if (insn->opcode == OP_SETVAL && insn->target == pseudo) {
			struct expression *expr = insn->val;
			struct basic_block *target;
			int true = 1; /* A symbol address is always considered true.. */

			if (expr) {
				switch (expr->type) {
				default:
					continue;
				case EXPR_VALUE:
					true = !!expr->value;
					break;
				}
			}
			target = true ? second->bb_true : second->bb_false;
			if (br->bb_true == bb)
				rewrite_branch(source, &br->bb_true, bb, target);
			if (br->bb_false == bb)
				rewrite_branch(source, &br->bb_false, bb, target);
		}
	} END_FOR_EACH_PTR(phi);
}

static inline int linearize_insn_list(struct instruction_list *list, struct instruction **arr, int nr)
{
	return linearize_ptr_list((struct ptr_list *)list, (void **)arr, nr);
}

static void simplify_phi_nodes(struct entrypoint *ep)
{
	struct basic_block *bb;

	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insns[2], *first, *second;

		if (linearize_insn_list(bb->insns, insns, 2) < 2)
			continue;

		first = insns[0];
		second = insns[1];

		if (first->opcode != OP_PHI)
			continue;
		if (second->opcode != OP_BR)
			continue;
		if (first->target != second->cond)
			continue;
		try_to_simplify_bb(ep, bb, first, second);
	} END_FOR_EACH_PTR(bb);
}

static void create_phi_copy(struct basic_block *bb, struct instruction *phi,
		pseudo_t src, pseudo_t dst)
{
	struct instruction *insn = last_instruction(bb->insns);
	struct instruction *new = alloc_instruction(OP_MOV, phi->type);
	struct instruction *dead = alloc_instruction(OP_DEAD, NULL);

	delete_last_instruction(&bb->insns);
	new->target = dst;
	new->src = src;
	add_instruction(&bb->insns, new);

	dead->target = src;
	add_instruction(&bb->insns, dead);

	/* And add back the last instruction */
	add_instruction(&bb->insns, insn);
}

static void remove_one_phi_node(struct entrypoint *ep,
	struct basic_block *bb,
	struct instruction *insn)
{
	struct phi *node;
	pseudo_t target = insn->target;

	FOR_EACH_PTR(insn->phi_list, node) {
		create_phi_copy(node->source, insn, node->pseudo, target);
	} END_FOR_EACH_PTR(node);
}

static void remove_phi_nodes(struct entrypoint *ep)
{
	struct basic_block *bb;
	FOR_EACH_PTR(ep->bbs, bb) {
		struct instruction *insn = first_instruction(bb->insns);
		if (insn && insn->opcode == OP_PHI)
			remove_one_phi_node(ep, bb, insn);
	} END_FOR_EACH_PTR(bb);
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
			struct basic_block *bb = alloc_basic_block(sym->pos);
			pseudo_t result;

			ep->name = sym;
			ep->entry = bb;
			bb->flags |= BB_REACHABLE;
			set_activeblock(ep, bb);
			concat_symbol_list(base_type->arguments, &ep->syms);
			result = linearize_statement(ep, base_type->stmt);
			if (bb_reachable(ep->active) && !bb_terminated(ep->active)) {
				struct symbol *ret_type = base_type->ctype.base_type;
				struct instruction *insn = alloc_instruction(OP_RET, ret_type);
				
				insn->src = result;
				add_one_insn(ep, insn);
			}
			pack_basic_blocks(ep);

			/*
			 * Questionable conditional branch simplification.
			 * This short-circuits branches to conditional branches,
			 * and leaves the phi-nodes "dangling" in the old
			 * basic block - the nodes are no longer attached to
			 * where the uses are. But it can still be considered
			 * SSA if you just look at it sideways..
			 */
			simplify_phi_nodes(ep);

			/*
			 * WARNING!! The removal of phi nodes will make the
			 * tree no longer valid SSA format. We do it here
			 * to make the linearized code look more like real
			 * assembly code, but we should do all the SSA-based
			 * optimizations (CSE etc) _before_ this point.
			 */
			remove_phi_nodes(ep);
			ret_ep = ep;
		}
	}

	return ret_ep;
}
