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
#include <stddef.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt);
pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr);

static void add_setcc(struct entrypoint *ep, struct expression *expr, pseudo_t val);
static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right);
static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val);

struct access_data;
static pseudo_t add_load(struct entrypoint *ep, struct access_data *);
pseudo_t linearize_initializer(struct entrypoint *ep, struct expression *initializer, struct access_data *);

struct pseudo void_pseudo = {};

unsigned long bb_generation;

static struct instruction *alloc_instruction(int opcode, struct symbol *type)
{
	struct instruction * insn = __alloc_instruction(0);
	insn->type = type;
	insn->opcode = opcode;
	return insn;
}

static struct entrypoint *alloc_entrypoint(void)
{
	bb_generation = 0;
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

static inline void add_pseudo_ptr(pseudo_t *ptr, struct pseudo_ptr_list **list)
{
	add_ptr_list((struct ptr_list **)list, ptr);
}

static inline void use_pseudo(pseudo_t p, pseudo_t *pp)
{
	*pp = p;
	if (p && p->type != PSEUDO_VOID)
		add_pseudo_ptr(pp, &p->users);
}

static struct phi* alloc_phi(struct basic_block *source, pseudo_t pseudo)
{
	struct phi *phi = __alloc_phi(0);
	phi->source = source;
	add_phi(&source->phinodes, phi);
	use_pseudo(pseudo, &phi->pseudo);
	return phi;
}

static inline int regno(pseudo_t n)
{
	int retval = -1;
	if (n && n->type == PSEUDO_REG)
		retval = n->nr;
	return retval;
}

static const char *show_pseudo(pseudo_t pseudo)
{
	static int n;
	static char buffer[4][64];
	char *buf;

	if (!pseudo)
		return "no pseudo";
	if (pseudo == VOID)
		return "VOID";
	buf = buffer[3 & ++n];
	switch(pseudo->type) {
	case PSEUDO_SYM: {
		struct symbol *sym = pseudo->sym;
		struct expression *expr;

		if (sym->bb_target) {
			snprintf(buf, 64, ".L%p", sym->bb_target);
			break;
		}
		if (sym->ident) {
			snprintf(buf, 64, "%s:%p", show_ident(sym->ident), sym);
			break;
		}
		expr = sym->initializer;
		if (!expr) {
			snprintf(buf, 64, "<anon sym: %d>", pseudo->nr);
			break;
		}
		switch (expr->type) {
		case EXPR_VALUE:
			snprintf(buf, 64, "<symbol value: %lld>", expr->value);
			break;
		case EXPR_STRING:
			return show_string(expr->string);
		default:
			snprintf(buf, 64, "<symbol expression: %d>", pseudo->nr);
			break;
		}
	}
	case PSEUDO_REG:
		snprintf(buf, 64, "%%r%d", pseudo->nr);
		break;
	case PSEUDO_VAL: {
		long long value = pseudo->value;
		if (value > 1000 || value < -1000)
			snprintf(buf, 64, "$%#llx", value);
		else
			snprintf(buf, 64, "$%lld", value);
		break;
	}
	case PSEUDO_ARG:
		snprintf(buf, 64, "%%arg%d", pseudo->nr);
		break;
	default:
		snprintf(buf, 64, "<bad pseudo type %d>", pseudo->type);
	}
	return buf;
}

static void show_instruction(struct instruction *insn)
{
	int op = insn->opcode;

	switch (op) {
	case OP_BADOP:
		printf("\tAIEEE! (%s <- %s)\n", show_pseudo(insn->target), show_pseudo(insn->src));
		break;
	case OP_RET:
		if (insn->type && insn->type != &void_ctype)
			printf("\tret %s\n", show_pseudo(insn->src));
		else
			printf("\tret\n");
		break;
	case OP_BR:
		if (insn->bb_true && insn->bb_false) {
			printf("\tbr\t%s, .L%p, .L%p\n", show_pseudo(insn->cond), insn->bb_true, insn->bb_false);
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
		printf("\tswitch %s", show_pseudo(insn->cond));
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
		printf("\tjmp *%s", show_pseudo(insn->target));
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			printf(", .L%p", jmp->target);
		} END_FOR_EACH_PTR(jmp);
		printf("\n");
		break;
	}
	
	case OP_PHI: {
		struct phi *phi;
		const char *s = " ";
		printf("\t%s <- phi", show_pseudo(insn->target));
		FOR_EACH_PTR(insn->phi_list, phi) {
			printf("%s(%s, .L%p)", s, show_pseudo(phi->pseudo), phi->source);
			s = ", ";
		} END_FOR_EACH_PTR(phi);
		printf("\n");
		break;
	}	
	case OP_LOAD:
		printf("\tload %s <- %d.%d.%d[%s]\n", show_pseudo(insn->target), insn->offset,
			insn->type->bit_offset, insn->type->bit_size, show_pseudo(insn->src));
		break;
	case OP_STORE:
		printf("\tstore %s -> %d.%d.%d[%s]\n", show_pseudo(insn->target), insn->offset,
			insn->type->bit_offset, insn->type->bit_size, show_pseudo(insn->src));
		break;
	case OP_CALL: {
		struct pseudo *arg;
		printf("\t%s <- CALL %s", show_pseudo(insn->target), show_pseudo(insn->func));
		FOR_EACH_PTR(insn->arguments, arg) {
			printf(", %s", show_pseudo(arg));
		} END_FOR_EACH_PTR(arg);
		printf("\n");
		break;
	}
	case OP_CAST:
		printf("\t%%r%d <- CAST(%d->%d) %s\n",
			regno(insn->target),
			insn->orig_type->bit_size, insn->type->bit_size, 
			show_pseudo(insn->src));
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
		printf("\t%%r%d <- %s  %s, %s\n",
			regno(insn->target),
			opname[op - OP_BINARY], show_pseudo(insn->src1), show_pseudo(insn->src2));
		break;
	}

	case OP_SLICE:
		printf("\t%%r%d <- slice  %s, %d, %d\n",
			regno(insn->target),
			show_pseudo(insn->base), insn->from, insn->len);
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
		printf("\t%%r%d <- %s  %s, %s\n",
			regno(insn->target),
			opname[op - OP_BINCMP], show_pseudo(insn->src1), show_pseudo(insn->src2));
		break;
	}

	case OP_NOT: case OP_NEG:
		printf("\t%%r%d <- %s %s\n",
			regno(insn->target),
			op == OP_NOT ? "not" : "neg", show_pseudo(insn->src1));
		break;
	case OP_SETCC:
		printf("\tsetcc %s\n", show_pseudo(insn->src));
		break;
	case OP_CONTEXT:
		printf("\tcontext %d\n", insn->increment);
		break;
	case OP_SNOP:
		printf("\tnop (%s -> %d.%d.%d[%s])\n", show_pseudo(insn->target), insn->offset,
			insn->type->bit_offset, insn->type->bit_size, show_pseudo(insn->src));
		break;
	case OP_LNOP:
		printf("\tnop (%s <- %d.%d.%d[%s])\n", show_pseudo(insn->target), insn->offset,
			insn->type->bit_offset, insn->type->bit_size, show_pseudo(insn->src));
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

	if (bb->children) {
		struct basic_block *to;
		FOR_EACH_PTR(bb->children, to) {
			printf("  **to %p (%s:%d:%d)**\n", to,
				input_streams[to->pos.stream].name, to->pos.line, to->pos.pos);
		} END_FOR_EACH_PTR(to);
	}

	if (bb->phinodes) {
		struct phi *phi;
		FOR_EACH_PTR(bb->phinodes, phi) {
			printf("  **phi source %s**\n", show_pseudo(phi->pseudo));
		} END_FOR_EACH_PTR(phi);
	}

	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR(insn);
	if (!bb_terminated(bb))
		printf("\tEND\n");
	printf("\n");
}

#define container(ptr, type, member) \
	(type *)((void *)(ptr) - offsetof(type, member))

static void show_symbol_usage(pseudo_t pseudo)
{
	if (pseudo) {
		pseudo_t *pp;
		FOR_EACH_PTR(pseudo->users, pp) {
			struct instruction *insn = container(pp, struct instruction, src);
			show_instruction(insn);
		} END_FOR_EACH_PTR(pp);
	}
}

void show_entry(struct entrypoint *ep)
{
	struct symbol *sym;
	struct basic_block *bb;

	printf("ep %p: %s\n", ep, show_ident(ep->name->ident));

	FOR_EACH_PTR(ep->syms, sym) {
		printf("   sym: %p %s\n", sym, show_ident(sym->ident));
		if (sym->ctype.modifiers & (MOD_EXTERN | MOD_STATIC | MOD_ADDRESSABLE))
			printf("\texternal visibility\n");
		show_symbol_usage(sym->pseudo);
	} END_FOR_EACH_PTR(sym);

	printf("\n");

	FOR_EACH_PTR(ep->bbs, bb) {
		if (!bb)
			continue;
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
		bb = alloc_basic_block(label->pos);
		label->bb_target = bb;
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
		add_bb(&src->children, dst);
		br->bb = src;
		add_instruction(&src->insns, br);
		ep->active = NULL;
	}
}

static void add_one_insn(struct entrypoint *ep, struct instruction *insn)
{
	struct basic_block *bb = ep->active;    

	if (bb_reachable(bb)) {
		insn->bb = bb;
		add_instruction(&bb->insns, insn);
	}
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
		use_pseudo(val, &cc->src);
		add_one_insn(ep, cc);
	}
}

static void add_branch(struct entrypoint *ep, struct expression *expr, pseudo_t cond, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *bb = ep->active;
	struct instruction *br;

	if (bb_reachable(bb)) {
       		br = alloc_instruction(OP_BR, expr->ctype);
		use_pseudo(cond, &br->cond);
		br->bb_true = bb_true;
		br->bb_false = bb_false;
		add_bb(&bb_true->parents, bb);
		add_bb(&bb_false->parents, bb);
		add_bb(&bb->children, bb_true);
		add_bb(&bb->children, bb_false);
		add_one_insn(ep, br);
	}
}

/* Dummy pseudo allocator */
static pseudo_t alloc_pseudo(struct instruction *def)
{
	static int nr = 0;
	struct pseudo * pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_REG;
	pseudo->nr = ++nr;
	pseudo->usage = 1;
	pseudo->def = def;
	return pseudo;
}

static pseudo_t symbol_pseudo(struct entrypoint *ep, struct symbol *sym)
{
	pseudo_t pseudo = sym->pseudo;

	if (!pseudo) {
		pseudo = __alloc_pseudo(0);
		pseudo->type = PSEUDO_SYM;
		pseudo->sym = sym;
		sym->pseudo = pseudo;
		add_symbol(&ep->accesses, sym);
	}
	/* Symbol pseudos have neither nr, usage nor def */
	return pseudo;
}

static pseudo_t value_pseudo(long long val)
{
	pseudo_t pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_VAL;
	pseudo->value = val;
	/* Value pseudos have neither nr, usage nor def */
	return pseudo;
}

static pseudo_t argument_pseudo(int nr)
{
	pseudo_t pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_ARG;
	pseudo->nr = nr;
	/* Argument pseudos have neither usage nor def */
	return pseudo;
}

/*
 * We carry the "access_data" structure around for any accesses,
 * which simplifies things a lot. It contains all the access
 * information in one place.
 */
struct access_data {
	struct symbol *ctype;
	pseudo_t address;		// pseudo containing address ..
	pseudo_t origval;		// pseudo for original value ..
	unsigned int offset, alignment;	// byte offset
	unsigned int bit_size, bit_offset; // which bits
	struct position pos;
};

static void finish_address_gen(struct entrypoint *ep, struct access_data *ad)
{
}

static int linearize_simple_address(struct entrypoint *ep,
	struct expression *addr,
	struct access_data *ad)
{
	if (addr->type == EXPR_SYMBOL) {
		ad->address = symbol_pseudo(ep, addr->symbol);
		return 1;
	}
	if (addr->type == EXPR_BINOP) {
		if (addr->right->type == EXPR_VALUE) {
			if (addr->op == '+') {
				ad->offset += get_expression_value(addr->right);
				return linearize_simple_address(ep, addr->left, ad);
			}
		}
	}
	ad->address = linearize_expression(ep, addr);
	return 1;
}

static int linearize_address_gen(struct entrypoint *ep,
	struct expression *expr,
	struct access_data *ad)
{
	struct symbol *ctype = expr->ctype;

	if (!ctype)
		return 0;
	ad->pos = expr->pos;
	ad->ctype = ctype;
	ad->bit_size = ctype->bit_size;
	ad->alignment = ctype->ctype.alignment;
	ad->bit_offset = ctype->bit_offset;
	if (expr->type == EXPR_PREOP && expr->op == '*')
		return linearize_simple_address(ep, expr->unop, ad);

	warning(expr->pos, "generating address of non-lvalue (%d)", expr->type);
	return 0;
}

static pseudo_t add_load(struct entrypoint *ep, struct access_data *ad)
{
	struct instruction *insn;
	pseudo_t new;

	new = ad->origval;
	if (new) {
		new->usage++;
		return new;
	}

	insn = alloc_instruction(OP_LOAD, ad->ctype);
	new = alloc_pseudo(insn);
	ad->origval = new;

	insn->target = new;
	insn->offset = ad->offset;
	use_pseudo(ad->address, &insn->src);
	add_one_insn(ep, insn);
	return new;
}

static void add_store(struct entrypoint *ep, struct access_data *ad, pseudo_t value)
{
	struct basic_block *bb = ep->active;

	if (bb_reachable(bb)) {
		struct instruction *store = alloc_instruction(OP_STORE, ad->ctype);
		store->offset = ad->offset;
		use_pseudo(value, &store->target);
		use_pseudo(ad->address, &store->src);
		add_one_insn(ep, store);
	}
}

/* Target-dependent, really.. */
#define OFFSET_ALIGN 8
#define MUST_ALIGN 0

static struct symbol *base_type(struct symbol *s)
{
	if (s->type == SYM_NODE)
		s = s->ctype.base_type;
	if (s->type == SYM_BITFIELD)
		s = s->ctype.base_type;
	return s;
}

static pseudo_t linearize_store_gen(struct entrypoint *ep,
		pseudo_t value,
		struct access_data *ad)
{
	unsigned long mask;
	pseudo_t shifted, ormask, orig, value_mask, newval;

	/* Bogus tests, but you get the idea.. */
	if (ad->bit_offset & (OFFSET_ALIGN-1))
		goto unaligned;
	if (ad->bit_size & (ad->bit_size-1))
		goto unaligned;
	if (ad->bit_size & (OFFSET_ALIGN-1))
		goto unaligned;
	if (MUST_ALIGN && ((ad->bit_size >> 3) & (ad->alignment-1)))
		goto unaligned;

	add_store(ep, ad, value);
	return value;

unaligned:
	mask = ((1<<ad->bit_size)-1) << ad->bit_offset;
	shifted = value;
	if (ad->bit_offset) {
		pseudo_t shift;
		shift = value_pseudo(ad->bit_offset);
		shifted = add_binary_op(ep, ad->ctype, OP_SHL, value, shift);
	}
	ad->ctype = base_type(ad->ctype);
	orig = add_load(ep, ad);
	ormask = value_pseudo(~mask);
	value_mask = add_binary_op(ep, ad->ctype, OP_AND, orig, ormask);
	newval = add_binary_op(ep, ad->ctype, OP_OR, orig, value_mask);

	add_store(ep, ad, newval);
	return value;
}

static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right)
{
	struct instruction *insn = alloc_instruction(op, ctype);
	pseudo_t target = alloc_pseudo(insn);
	insn->target = target;
	use_pseudo(left, &insn->src1);
	use_pseudo(right, &insn->src2);
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

static pseudo_t linearize_load_gen(struct entrypoint *ep, struct access_data *ad)
{
	pseudo_t new = add_load(ep, ad);

	if (ad->bit_offset) {
		pseudo_t shift = value_pseudo(ad->bit_offset);
		pseudo_t newval = add_binary_op(ep, ad->ctype, OP_SHR, new, shift);
		new = newval;
	}
		
	return new;
}

static pseudo_t linearize_access(struct entrypoint *ep, struct expression *expr)
{
	struct access_data ad = { NULL, };
	pseudo_t value;

	if (!linearize_address_gen(ep, expr, &ad))
		return VOID;
	value = linearize_load_gen(ep, &ad);
	finish_address_gen(ep, &ad);
	return value;
}

/* FIXME: FP */
static pseudo_t linearize_inc_dec(struct entrypoint *ep, struct expression *expr, int postop)
{
	struct access_data ad = { NULL, };
		pseudo_t old, new, one;
	int op = expr->op == SPECIAL_INCREMENT ? OP_ADD : OP_SUB;

	if (!linearize_address_gen(ep, expr->unop, &ad))
		return VOID;

	old = linearize_load_gen(ep, &ad);
	one = value_pseudo(1);
	new = add_binary_op(ep, expr->ctype, op, old, one);
	linearize_store_gen(ep, new, &ad);
	finish_address_gen(ep, &ad);
	return postop ? old : new;
}

static pseudo_t add_uniop(struct entrypoint *ep, struct expression *expr, int op, pseudo_t src)
{
	struct instruction *insn = alloc_instruction(op, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	use_pseudo(src, &insn->src1);
	add_one_insn(ep, insn);
	return new;
}

static pseudo_t linearize_slice(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t pre = linearize_expression(ep, expr->base);
	struct instruction *insn = alloc_instruction(OP_SLICE, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	insn->from = expr->r_bitpos;
	insn->len = expr->r_nrbits;
	use_pseudo(pre, &insn->base);
	add_one_insn(ep, insn);
	return new;
}

static pseudo_t linearize_regular_preop(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t pre = linearize_expression(ep, expr->unop);
	switch (expr->op) {
	case '+':
		return pre;
	case '!': {
		pseudo_t zero = value_pseudo(0);
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
	struct access_data ad = { NULL, };
	struct expression *target = expr->left;
	pseudo_t value;

	value = linearize_expression(ep, expr->right);
	if (!linearize_address_gen(ep, target, &ad))
		return VOID;
	if (expr->op != '=') {
		pseudo_t oldvalue = linearize_load_gen(ep, &ad);
		pseudo_t dst;
		static const int op_trans[] = {
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
		dst = add_binary_op(ep, expr->ctype, op_trans[expr->op - SPECIAL_BASE], oldvalue, value);
		value = dst;
	}
	value = linearize_store_gen(ep, value, &ad);
	finish_address_gen(ep, &ad);
	return value;
}

static pseudo_t linearize_call_expression(struct entrypoint *ep, struct expression *expr)
{
	struct expression *arg, *fn;
	struct instruction *insn = alloc_instruction(OP_CALL, expr->ctype);
	pseudo_t retval, call;
	int context_diff;

	if (!expr->ctype) {
		warning(expr->pos, "call with no type!");
		return VOID;
	}

	FOR_EACH_PTR(expr->args, arg) {
		pseudo_t new = linearize_expression(ep, arg);
		use_pseudo(new, add_pseudo(&insn->arguments, new));
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
	if (fn->type == EXPR_SYMBOL) {
		call = symbol_pseudo(ep, fn->symbol);
	} else {
		call = linearize_expression(ep, fn);
	}
	use_pseudo(call, &insn->func);
	retval = VOID;
	if (expr->ctype != &void_ctype)
		retval = alloc_pseudo(insn);
	insn->target = retval;
	add_one_insn(ep, insn);

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
	if (!expr->cond_true)
		true = cond;
	res = add_binary_op(ep, expr->ctype, OP_SEL, true, false);
	return res;
}

static pseudo_t add_join_conditional(struct entrypoint *ep, struct expression *expr,
				     pseudo_t src1, struct basic_block *bb1,
				     pseudo_t src2, struct basic_block *bb2)
{
	pseudo_t target;
	struct instruction *phi_node;
	struct phi *phi1, *phi2;

	if (src1 == VOID || !bb_reachable(bb1))
		return src2;
	if (src2 == VOID || !bb_reachable(bb2))
		return src1;
	phi_node = alloc_instruction(OP_PHI, expr->ctype);
	phi1 = alloc_phi(bb1, src1);
	phi2 = alloc_phi(bb2, src2);
	add_phi(&phi_node->phi_list, phi1);
	add_phi(&phi_node->phi_list, phi2);
	phi_node->target = target = alloc_pseudo(phi_node);
	add_one_insn(ep, phi_node);
	return target;
}	

static pseudo_t linearize_short_conditional(struct entrypoint *ep, struct expression *expr,
					    struct expression *cond,
					    struct expression *expr_false)
{
	pseudo_t src1, src2;
	struct basic_block *bb_true;
	struct basic_block *bb_false = alloc_basic_block(expr_false->pos);
	struct basic_block *merge = alloc_basic_block(expr->pos);

	src1 = linearize_expression(ep, cond);
	bb_true = ep->active;
	add_branch(ep, expr, src1, merge, bb_false);

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
	if (expr->ctype->bit_size < 0)
		return VOID;
	insn = alloc_instruction(OP_CAST, expr->ctype);
	result = alloc_pseudo(insn);
	insn->target = result;
	insn->orig_type = expr->cast_expression->ctype;
	use_pseudo(src, &insn->src);
	add_one_insn(ep, insn);
	return result;
}

pseudo_t linearize_position(struct entrypoint *ep, struct expression *pos, struct access_data *ad)
{
	struct expression *init_expr = pos->init_expr;
	pseudo_t value = linearize_expression(ep, init_expr);

	ad->offset = pos->init_offset;	
	ad->ctype = init_expr->ctype;
	linearize_store_gen(ep, value, ad);
	return VOID;
}

pseudo_t linearize_initializer(struct entrypoint *ep, struct expression *initializer, struct access_data *ad)
{
	switch (initializer->type) {
	case EXPR_INITIALIZER: {
		struct expression *expr;
		FOR_EACH_PTR(initializer->expr_list, expr) {
			linearize_initializer(ep, expr, ad);
		} END_FOR_EACH_PTR(expr);
		break;
	}
	case EXPR_POS:
		linearize_position(ep, initializer, ad);
		break;
	default: {
		pseudo_t value = linearize_expression(ep, initializer);
		ad->ctype = initializer->ctype;
		linearize_store_gen(ep, value, ad);
	}
	}

	return VOID;
}

void linearize_argument(struct entrypoint *ep, struct symbol *arg, int nr)
{
	struct access_data ad = { NULL, };

	ad.ctype = arg;
	ad.address = symbol_pseudo(ep, arg);
	linearize_store_gen(ep, argument_pseudo(nr), &ad);
	finish_address_gen(ep, &ad);
}

pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr)
{
	if (!expr)
		return VOID;

	switch (expr->type) {
	case EXPR_SYMBOL:
		return add_setval(ep, expr->symbol, NULL);

	case EXPR_VALUE:
		return value_pseudo(expr->value);

	case EXPR_STRING: case EXPR_FVALUE: case EXPR_LABEL:
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

	case EXPR_COMMA:
		linearize_expression(ep, expr->left);
		return linearize_expression(ep, expr->right);

	case EXPR_ASSIGNMENT:
		return linearize_assignment(ep, expr);

	case EXPR_PREOP:
		return linearize_preop(ep, expr);

	case EXPR_POSTOP:
		return linearize_postop(ep, expr);

	case EXPR_CAST:
	case EXPR_IMPLIED_CAST:
		return linearize_cast(ep, expr);
	
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
	struct access_data ad = { NULL, };

	if (!sym->initializer)
		return;

	ad.address = symbol_pseudo(ep, sym);
	linearize_initializer(ep, sym->initializer, &ad);
	finish_address_gen(ep, &ad);
}

static pseudo_t linearize_compound_statement(struct entrypoint *ep, struct statement *stmt)
{
	pseudo_t pseudo;
	struct statement *s;
	struct symbol *sym;
	struct symbol *ret = stmt->ret;

	concat_symbol_list(stmt->syms, &ep->syms);

	FOR_EACH_PTR(stmt->syms, sym) {
		linearize_one_symbol(ep, sym);
	} END_FOR_EACH_PTR(sym);

	pseudo = VOID;
	FOR_EACH_PTR(stmt->stmts, s) {
		pseudo = linearize_statement(ep, s);
	} END_FOR_EACH_PTR(s);

	if (ret) {
		struct basic_block *bb = get_bound_block(ep, ret);
		struct instruction *phi = first_instruction(bb->insns);

		set_activeblock(ep, bb);
		if (!phi)
			return pseudo;

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
		struct basic_block *bb_return = get_bound_block(ep, stmt->ret_target);
		struct basic_block *active;
		pseudo_t src = linearize_expression(ep, expr);
		active = ep->active;
		add_goto(ep, bb_return);
		if (active && src != &void_pseudo) {
			struct instruction *phi_node = first_instruction(bb_return->insns);
			struct phi *phi;
			if (!phi_node) {
				phi_node = alloc_instruction(OP_PHI, expr->ctype);
				phi_node->target = alloc_pseudo(phi_node);
				phi_node->bb = bb_return;
				add_instruction(&bb_return->insns, phi_node);
			}
			phi = alloc_phi(active, src);
			add_phi(&phi_node->phi_list, phi);
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
		struct basic_block *active;
		pseudo_t pseudo;

		active = ep->active;
		if (!bb_reachable(active))
			break;

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
		use_pseudo(pseudo, &goto_ins->target);
		add_one_insn(ep, goto_ins);

		FOR_EACH_PTR(stmt->target_list, sym) {
			struct basic_block *bb_computed = get_bound_block(ep, sym);
			struct multijmp *jmp = alloc_multijmp(bb_computed, 1, 0);
			add_multijmp(&goto_ins->multijmp_list, jmp);
			add_bb(&bb_computed->parents, ep->active);
			add_bb(&active->children, bb_computed);
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
		struct basic_block *active;
		pseudo_t pseudo;

		pseudo = linearize_expression(ep, stmt->switch_expression);

		active = ep->active;
		if (!bb_reachable(active))
			break;

		switch_ins = alloc_instruction(OP_SWITCH, NULL);
		use_pseudo(pseudo, &switch_ins->cond);
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
			add_bb(&bb_case->parents, active);
			add_bb(&active->children, bb_case);
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
			set_activeblock(ep, loop_top);
		}

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

/*
 * Return the known truth value of a pseudo, or -1 if
 * it's not known.
 */
static int pseudo_truth_value(pseudo_t pseudo)
{
	switch (pseudo->type) {
	case PSEUDO_VAL:
		return !!pseudo->value;

	case PSEUDO_REG: {
		struct instruction *insn = pseudo->def;
		if (insn->opcode == OP_SETVAL && insn->target == pseudo) {
			struct expression *expr = insn->val;

			/* A symbol address is always considered true.. */
			if (!expr)
				return 1;
			if (expr->type == EXPR_VALUE)
				return !!expr->value;
		}
	}
		/* Fall through */
	default:
		return -1;
	}
}


static void try_to_simplify_bb(struct entrypoint *ep, struct basic_block *bb,
	struct instruction *first, struct instruction *second)
{
	struct phi *phi;

	FOR_EACH_PTR(first->phi_list, phi) {
		struct basic_block *source = phi->source, *target;
		pseudo_t pseudo = phi->pseudo;
		struct instruction *br;
		int true;

		br = last_instruction(source->insns);
		if (!br)
			continue;
		if (br->opcode != OP_BR)
			continue;

		true = pseudo_truth_value(pseudo);
		if (true < 0)
			continue;
		target = true ? second->bb_true : second->bb_false;
		if (br->bb_true == bb)
			rewrite_branch(source, &br->bb_true, bb, target);
		if (br->bb_false == bb)
			rewrite_branch(source, &br->bb_false, bb, target);
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

static inline void concat_user_list(struct pseudo_ptr_list *src, struct pseudo_ptr_list **dst)
{
	concat_ptr_list((struct ptr_list *)src, (struct ptr_list **)dst);
}

static void convert_load_insn(struct instruction *insn, pseudo_t src)
{
	pseudo_t target, *usep;

	/*
	 * Go through the "insn->users" list and replace them all..
	 */
	target = insn->target;
	FOR_EACH_PTR(target->users, usep) {
		*usep = src;
	} END_FOR_EACH_PTR(usep);
	concat_user_list(target->users, &src->users);

	/* Turn the load into a no-op */
	insn->opcode = OP_LNOP;
}

static int overlapping_memop(struct instruction *a, struct instruction *b)
{
	unsigned int a_start = (a->offset << 3) + a->type->bit_offset;
	unsigned int b_start = (b->offset << 3) + b->type->bit_offset;
	unsigned int a_size = a->type->bit_size;
	unsigned int b_size = b->type->bit_size;

	if (a_size + a_start <= b_start)
		return 0;
	if (b_size + b_start <= a_start)
		return 0;
	return 1;
}

static inline int same_memop(struct instruction *a, struct instruction *b)
{
	return	a->offset == b->offset &&
		a->type->bit_size == b->type->bit_size &&
		a->type->bit_offset == b->type->bit_offset;
}

/*
 * Return 1 if "one" dominates the access to 'pseudo'
 * in insn.
 *
 * Return 0 if it doesn't, and -1 if you don't know.
 */
static int dominates(pseudo_t pseudo, struct instruction *insn,
	struct instruction *one, int local)
{
	int opcode = one->opcode;

	if (opcode == OP_CALL)
		return local ? 0 : -1;
	if (opcode != OP_LOAD && opcode != OP_STORE)
		return 0;
	if (one->src != pseudo) {
		if (local)
			return 0;
		/* We don't think two explicitly different symbols ever alias */
		if (one->src->type == PSEUDO_SYM)
			return 0;
		/* We could try to do some alias analysis here */
		return -1;
	}
	if (!same_memop(insn, one)) {
		if (one->opcode == OP_LOAD)
			return 0;
		if (!overlapping_memop(insn, one))
			return 0;
		return -1;
	}
	return 1;
}

static int find_dominating_parents(pseudo_t pseudo, struct instruction *insn,
	struct basic_block *bb, unsigned long generation, struct phi_list **dominators,
	int local)
{
	struct basic_block *parent;

	FOR_EACH_PTR(bb->parents, parent) {
		struct instruction *one;
		struct phi *phi;

		FOR_EACH_PTR_REVERSE(parent->insns, one) {
			int dominance;
			if (one == insn)
				goto no_dominance;
			dominance = dominates(pseudo, insn, one, local);
			if (dominance < 0)
				return 0;
			if (!dominance)
				continue;
			goto found_dominator;
		} END_FOR_EACH_PTR_REVERSE(one);
no_dominance:
		if (parent->generation == generation)
			continue;
		parent->generation = generation;

		if (!find_dominating_parents(pseudo, insn, parent, generation, dominators, local))
			return 0;
		continue;

found_dominator:
		phi = alloc_phi(parent, one->target);
		add_phi(dominators, phi);
	} END_FOR_EACH_PTR(parent);
	return 1;
}		

static int find_dominating_stores(pseudo_t pseudo, struct instruction *insn,
	unsigned long generation, int local)
{
	struct basic_block *bb = insn->bb;
	struct instruction *one, *dom = NULL;
	struct phi_list *dominators;

	/* Unreachable load? Undo it */
	if (!bb) {
		insn->opcode = OP_LNOP;
		return 1;
	}

	FOR_EACH_PTR(bb->insns, one) {
		int dominance;
		if (one == insn)
			goto found;
		dominance = dominates(pseudo, insn, one, local);
		if (dominance < 0) {
			dom = NULL;
			continue;
		}
		if (!dominance)
			continue;
		dom = one;
	} END_FOR_EACH_PTR(one);
	/* Whaa? */
	warning(pseudo->sym->pos, "unable to find symbol read");
	return 0;
found:
	if (dom) {
		convert_load_insn(insn, dom->target);
		return 1;
	}

	/* Ok, go find the parents */
	bb->generation = generation;

	dominators = NULL;
	if (!find_dominating_parents(pseudo, insn, bb, generation, &dominators, local))
		return 0;

	/* This happens with initial assignments to structures etc.. */
	if (!dominators) {
		if (!local)
			return 0;
		convert_load_insn(insn, value_pseudo(0));
		return 1;
	}

	/*
	 * If we find just one dominating instruction, we
	 * can turn it into a direct thing. Otherwise we'll
	 * have to turn the load into a phi-node of the
	 * dominators.
	 */
	if (phi_list_size(dominators) == 1) {
		struct phi * phi = first_phi(dominators);
		phi->source = NULL;	/* Mark it as not used */
		convert_load_insn(insn, phi->pseudo);
	} else {
		pseudo_t new = alloc_pseudo(insn);
		convert_load_insn(insn, new);

		/*
		 * FIXME! This is dubious. We should probably allocate a new
		 * instruction instead of re-using the OP_LOAD instruction.
		 * Re-use of the instruction makes the usage list suspect.
		 *
		 * It should be ok, because the only usage of the OP_LOAD
		 * is the symbol pseudo, and we should never follow that
		 * list _except_ for exactly the dominant instruction list
		 * generation (and then we always check the opcode).
		 */
		insn->opcode = OP_PHI;
		insn->bb = bb;
		insn->target = new;
		insn->phi_list = dominators;
	}
	return 1;
}

/* Kill a pseudo that is dead on exit from the bb */
static void kill_dead_stores(pseudo_t pseudo, unsigned long generation, struct basic_block *bb, int local)
{
	struct instruction *insn;
	struct basic_block *parent;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR_REVERSE(bb->insns, insn) {
		int opcode = insn->opcode;

		if (opcode != OP_LOAD && opcode != OP_STORE)
			continue;
		if (insn->src == pseudo) {
			if (opcode == OP_LOAD)
				return;
			insn->opcode = OP_SNOP;
			continue;
		}
		if (local)
			continue;
		if (insn->src->type != PSEUDO_SYM)
			return;
	} END_FOR_EACH_PTR_REVERSE(insn);

	FOR_EACH_PTR(bb->parents, parent) {
		struct basic_block *child;
		FOR_EACH_PTR(parent->children, child) {
			if (child && child != bb)
				return;
		} END_FOR_EACH_PTR(child);
		kill_dead_stores(pseudo, generation, parent, local);
	} END_FOR_EACH_PTR(parent);
}

/*
 * This should see if the "insn" trivially dominates some previous store, and kill the
 * store if unnecessary.
 */
static void kill_dominated_stores(pseudo_t pseudo, struct instruction *insn, 
	unsigned long generation, struct basic_block *bb, int local, int found)
{
	struct instruction *one;
	struct basic_block *parent;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR_REVERSE(bb->insns, one) {
		int dominance;
		if (!found) {
			if (one != insn)
				continue;
			found = 1;
			continue;
		}
		dominance = dominates(pseudo, insn, one, local);
		if (!dominance)
			continue;
		if (dominance < 0)
			return;
		if (one->opcode == OP_LOAD)
			return;
		one->opcode = OP_SNOP;
	} END_FOR_EACH_PTR_REVERSE(one);

	if (!found) {
		warning(bb->pos, "Unable to find instruction");
		return;
	}

	FOR_EACH_PTR(bb->parents, parent) {
		struct basic_block *child;
		FOR_EACH_PTR(parent->children, child) {
			if (child && child != bb)
				return;
		} END_FOR_EACH_PTR(child);
		kill_dominated_stores(pseudo, insn, generation, parent, local, found);
	} END_FOR_EACH_PTR(parent);
}

static void simplify_one_symbol(struct entrypoint *ep, struct symbol *sym)
{
	pseudo_t pseudo, src, *pp;
	struct instruction *def;
	unsigned long mod;
	int all;

	/* Never used as a symbol? */
	pseudo = sym->pseudo;
	if (!pseudo)
		return;

	/* We don't do coverage analysis of volatiles.. */
	if (sym->ctype.modifiers & MOD_VOLATILE)
		return;

	/* ..and symbols with external visibility need more care */
	mod = sym->ctype.modifiers & (MOD_EXTERN | MOD_STATIC | MOD_ADDRESSABLE);
	if (mod)
		goto external_visibility;

	def = NULL;
	FOR_EACH_PTR(pseudo->users, pp) {
		/* We know that the symbol-pseudo use is the "src" in the instruction */
		struct instruction *insn = container(pp, struct instruction, src);

		switch (insn->opcode) {
		case OP_STORE:
			if (def)
				goto multi_def;
			def = insn;
			break;
		case OP_LOAD:
			break;
		default:
			warning(sym->pos, "symbol '%s' pseudo used in unexpected way", show_ident(sym->ident));
		}
		if (insn->offset)
			goto complex_def;
	} END_FOR_EACH_PTR(pp);

	/*
	 * Goodie, we have a single store (if even that) in the whole
	 * thing. Replace all loads with moves from the pseudo,
	 * replace the store with a def.
	 */
	src = VOID;
	if (def) {
		src = def->target;		

		/* Turn the store into a no-op */
		def->opcode = OP_SNOP;
	}
	FOR_EACH_PTR(pseudo->users, pp) {
		struct instruction *insn = container(pp, struct instruction, src);
		if (insn->opcode == OP_LOAD)
			convert_load_insn(insn, src);
	} END_FOR_EACH_PTR(pp);
	return;

multi_def:
complex_def:
external_visibility:
	all = 1;
	FOR_EACH_PTR(pseudo->users, pp) {
		struct instruction *insn = container(pp, struct instruction, src);
		if (insn->opcode == OP_LOAD)
			all &= find_dominating_stores(pseudo, insn, ++bb_generation, !mod);
	} END_FOR_EACH_PTR(pp);

	/* If we converted all the loads, remove the stores. They are dead */
	if (all && !mod) {
		FOR_EACH_PTR(pseudo->users, pp) {
			struct instruction *insn = container(pp, struct instruction, src);
			if (insn->opcode == OP_STORE)
				insn->opcode = OP_SNOP;
		} END_FOR_EACH_PTR(pp);
	} else {
		/*
		 * If we couldn't take the shortcut, see if we can at least kill some
		 * of them..
		 */
		FOR_EACH_PTR(pseudo->users, pp) {
			struct instruction *insn = container(pp, struct instruction, src);
			if (insn->opcode == OP_STORE)
				kill_dominated_stores(pseudo, insn, ++bb_generation, insn->bb, !mod, 0);
		} END_FOR_EACH_PTR(pp);

		if (!(mod & (MOD_EXTERN | MOD_STATIC))) {
			struct basic_block *bb;
			FOR_EACH_PTR(ep->bbs, bb) {
				if (!bb->children)
					kill_dead_stores(pseudo, ++bb_generation, bb, !mod);
			} END_FOR_EACH_PTR(bb);
		}
	}
			
	return;
}

static void simplify_symbol_usage(struct entrypoint *ep)
{
	struct symbol *sym;
	FOR_EACH_PTR(ep->accesses, sym) {
		simplify_one_symbol(ep, sym);
		sym->pseudo = NULL;
	} END_FOR_EACH_PTR(sym);
}

static void mark_bb_reachable(struct basic_block *bb, unsigned long generation)
{
	struct basic_block *child;

	if (bb->generation == generation)
		return;
	bb->generation = generation;
	FOR_EACH_PTR(bb->children, child) {
		mark_bb_reachable(child, generation);
	} END_FOR_EACH_PTR(child);
}

static void kill_unreachable_bbs(struct entrypoint *ep)
{
	struct basic_block *bb;
	unsigned long generation = ++bb_generation;

	mark_bb_reachable(ep->entry, generation);
	FOR_EACH_PTR(ep->bbs, bb) {
		struct phi *phi;
		if (bb->generation == generation)
			continue;
		FOR_EACH_PTR(bb->phinodes, phi) {
			phi->pseudo = VOID;
			phi->source = NULL;
		} END_FOR_EACH_PTR(phi);
	} END_FOR_EACH_PTR(bb);
}

static void pack_basic_blocks(struct entrypoint *ep)
{
	struct basic_block *bb;

	kill_unreachable_bbs(ep);

	/* See if we can merge a bb into another one.. */
	FOR_EACH_PTR(ep->bbs, bb) {
		struct basic_block *parent, *child;

		if (!bb_reachable(bb))
			continue;
		/*
		 * If this block has a phi-node associated with it,
		 */
		if (bb->phinodes)
			continue;

		if (ep->entry == bb)
			continue;

		/*
		 * See if we only have one parent..
		 */
		if (bb_list_size(bb->parents) != 1)
			continue;
		parent = first_basic_block(bb->parents);
		if (parent == bb)
			continue;

		/*
		 * Goodie. See if the parent can merge..
		 */
		FOR_EACH_PTR(parent->children, child) {
			if (child != bb)
				goto no_merge;
		} END_FOR_EACH_PTR(child);

		parent->children = bb->children;
		FOR_EACH_PTR(bb->children, child) {
			struct basic_block *p;
			FOR_EACH_PTR(child->parents, p) {
				if (p != bb)
					continue;
				*THIS_ADDRESS(p) = parent;
			} END_FOR_EACH_PTR(p);
		} END_FOR_EACH_PTR(child);

		delete_last_instruction(&parent->insns);
		concat_instruction_list(bb->insns, &parent->insns);
		bb->parents = NULL;
		bb->children = NULL;
		bb->insns = NULL;

	no_merge:
		/* nothing to do */;
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
			struct symbol *arg;
			pseudo_t result;
			int i;

			ep->name = sym;
			ep->entry = bb;
			set_activeblock(ep, bb);
			concat_symbol_list(base_type->arguments, &ep->syms);

			/* FIXME!! We should do something else about varargs.. */
			i = 0;
			FOR_EACH_PTR(base_type->arguments, arg) {
				linearize_argument(ep, arg, ++i);
			} END_FOR_EACH_PTR(arg);

			result = linearize_statement(ep, base_type->stmt);
			if (bb_reachable(ep->active) && !bb_terminated(ep->active)) {
				struct symbol *ret_type = base_type->ctype.base_type;
				struct instruction *insn = alloc_instruction(OP_RET, ret_type);
				
				use_pseudo(result, &insn->src);
				add_one_insn(ep, insn);
			}

			simplify_symbol_usage(ep);

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
			 * Remove or merge basic blocks.
			 */
			pack_basic_blocks(ep);

			ret_ep = ep;
		}
	}

	return ret_ep;
}
