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
#include <assert.h>

#include "parse.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"
#include "target.h"

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt);
pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr);

static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right);
static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val);
static void linearize_one_symbol(struct entrypoint *ep, struct symbol *sym);

struct access_data;
static pseudo_t add_load(struct entrypoint *ep, struct access_data *);
pseudo_t linearize_initializer(struct entrypoint *ep, struct expression *initializer, struct access_data *);

struct pseudo void_pseudo = {};

static struct instruction *alloc_instruction(int opcode, int size)
{
	struct instruction * insn = __alloc_instruction(0);
	insn->opcode = opcode;
	insn->size = size;
	return insn;
}

static inline int type_size(struct symbol *type)
{
	return type ? type->bit_size > 0 ? type->bit_size : 0 : 0;
}

static struct instruction *alloc_typed_instruction(int opcode, struct symbol *type)
{
	return alloc_instruction(opcode, type_size(type));
}

static struct entrypoint *alloc_entrypoint(void)
{
	return __alloc_entrypoint(0);
}

static struct basic_block *alloc_basic_block(struct entrypoint *ep, struct position pos)
{
	struct basic_block *bb = __alloc_basic_block(0);
	bb->context = -1;
	bb->pos = pos;
	bb->ep = ep;
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

static inline int regno(pseudo_t n)
{
	int retval = -1;
	if (n && n->type == PSEUDO_REG)
		retval = n->nr;
	return retval;
}

const char *show_pseudo(pseudo_t pseudo)
{
	static int n;
	static char buffer[4][64];
	char *buf;
	int i;

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
			snprintf(buf, 64, "%s", show_ident(sym->ident));
			break;
		}
		expr = sym->initializer;
		snprintf(buf, 64, "<anon symbol:%p>", sym);
		switch (expr->type) {
		case EXPR_VALUE:
			snprintf(buf, 64, "<symbol value: %lld>", expr->value);
			break;
		case EXPR_STRING:
			return show_string(expr->string);
		default:
			break;
		}
		break;
	}
	case PSEUDO_REG:
		i = snprintf(buf, 64, "%%r%d", pseudo->nr);
		if (pseudo->ident)
			sprintf(buf+i, "(%s)", show_ident(pseudo->ident));
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
	case PSEUDO_PHI:
		i = snprintf(buf, 64, "%%phi%d", pseudo->nr);
		if (pseudo->ident)
			sprintf(buf+i, "(%s)", show_ident(pseudo->ident));
		break;
	default:
		snprintf(buf, 64, "<bad pseudo type %d>", pseudo->type);
	}
	return buf;
}

static const char* opcodes[] = {
	[OP_BADOP] = "bad_op",

	/* Fn entrypoint */
	[OP_ENTRY] = "<entry-point>",

	/* Terminator */
	[OP_RET] = "ret",
	[OP_BR] = "br",
	[OP_SWITCH] = "switch",
	[OP_INVOKE] = "invoke",
	[OP_COMPUTEDGOTO] = "jmp *",
	[OP_UNWIND] = "unwind",
	
	/* Binary */
	[OP_ADD] = "add",
	[OP_SUB] = "sub",
	[OP_MUL] = "mul",
	[OP_DIV] = "div",
	[OP_MOD] = "mod",
	[OP_SHL] = "shl",
	[OP_SHR] = "shr",
	
	/* Logical */
	[OP_AND] = "and",
	[OP_OR] = "or",
	[OP_XOR] = "xor",
	[OP_AND_BOOL] = "and-bool",
	[OP_OR_BOOL] = "or-bool",

	/* Binary comparison */
	[OP_SET_EQ] = "seteq",
	[OP_SET_NE] = "setne",
	[OP_SET_LE] = "setle",
	[OP_SET_GE] = "setge",
	[OP_SET_LT] = "setlt",
	[OP_SET_GT] = "setgt",
	[OP_SET_B] = "setb",
	[OP_SET_A] = "seta",
	[OP_SET_BE] = "setbe",
	[OP_SET_AE] = "setae",

	/* Uni */
	[OP_NOT] = "not",
	[OP_NEG] = "neg",

	/* Special three-input */
	[OP_SEL] = "select",
	
	/* Memory */
	[OP_MALLOC] = "malloc",
	[OP_FREE] = "free",
	[OP_ALLOCA] = "alloca",
	[OP_LOAD] = "load",
	[OP_STORE] = "store",
	[OP_SETVAL] = "set",
	[OP_GET_ELEMENT_PTR] = "getelem",

	/* Other */
	[OP_PHI] = "phi",
	[OP_PHISOURCE] = "phisrc",
	[OP_CAST] = "cast",
	[OP_PTRCAST] = "ptrcast",
	[OP_CALL] = "call",
	[OP_VANEXT] = "va_next",
	[OP_VAARG] = "va_arg",
	[OP_SLICE] = "slice",
	[OP_SNOP] = "snop",
	[OP_LNOP] = "lnop",
	[OP_NOP] = "nop",
	[OP_DEATHNOTE] = "dead",
	[OP_ASM] = "asm",

	/* Sparse tagging (line numbers, context, whatever) */
	[OP_CONTEXT] = "context",
};

void show_instruction(struct instruction *insn)
{
	int opcode = insn->opcode;
	static char buffer[1024] = "\t";
	char *buf;

	buf = buffer+1;
	if (!insn->bb) {
		if (verbose < 2)
			return;
		buf += sprintf(buf, "# ");
	}

	if (opcode < sizeof(opcodes)/sizeof(char *)) {
		const char *op = opcodes[opcode];
		if (!op)
			buf += sprintf(buf, "opcode:%d", opcode);
		else
			buf += sprintf(buf, "%s", op);
		if (insn->size)
			buf += sprintf(buf, ".%d", insn->size);
		memset(buf, ' ', 20);
		buf++;
	}

	if (buf < buffer + 12)
		buf = buffer + 12;
	switch (opcode) {
	case OP_RET:
		if (insn->src && insn->src != VOID)
			buf += sprintf(buf, "%s", show_pseudo(insn->src));
		break;
	case OP_BR:
		if (insn->bb_true && insn->bb_false) {
			buf += sprintf(buf, "%s, .L%p, .L%p", show_pseudo(insn->cond), insn->bb_true, insn->bb_false);
			break;
		}
		buf += sprintf(buf, ".L%p", insn->bb_true ? insn->bb_true : insn->bb_false);
		break;

	case OP_SETVAL: {
		struct expression *expr = insn->val;
		pseudo_t pseudo = insn->symbol;
		buf += sprintf(buf, "%s <- ", show_pseudo(insn->target));
		if (pseudo) {
			struct symbol *sym = pseudo->sym;
			if (!sym) {
				buf += sprintf(buf, "%s", show_pseudo(pseudo));
				break;
			}
			if (sym->bb_target) {
				buf += sprintf(buf, ".L%p", sym->bb_target);
				break;
			}
			if (sym->ident) {
				buf += sprintf(buf, "%s", show_ident(sym->ident));
				break;
			}
			buf += sprintf(buf, "<anon symbol:%p>", sym);
			break;
		}

		if (!expr) {
			buf += sprintf(buf, "%s", "<none>");
			break;
		}
			
		switch (expr->type) {
		case EXPR_VALUE:
			buf += sprintf(buf, "%lld", expr->value);
			break;
		case EXPR_FVALUE:
			buf += sprintf(buf, "%Lf", expr->fvalue);
			break;
		case EXPR_STRING:
			buf += sprintf(buf, "%.40s", show_string(expr->string));
			break;
		case EXPR_SYMBOL:
			buf += sprintf(buf, "%s", show_ident(expr->symbol->ident));
			break;
		case EXPR_LABEL:
			buf += sprintf(buf, ".L%p", expr->symbol->bb_target);
			break;
		default:
			buf += sprintf(buf, "SETVAL EXPR TYPE %d", expr->type);
		}
		break;
	}
	case OP_SWITCH: {
		struct multijmp *jmp;
		buf += sprintf(buf, "%s", show_pseudo(insn->target));
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			if (jmp->begin == jmp->end)
				buf += sprintf(buf, ", %d -> .L%p", jmp->begin, jmp->target);
			else if (jmp->begin < jmp->end)
				buf += sprintf(buf, ", %d ... %d -> .L%p", jmp->begin, jmp->end, jmp->target);
			else
				buf += sprintf(buf, ", default -> .L%p", jmp->target);
		} END_FOR_EACH_PTR(jmp);
		break;
	}
	case OP_COMPUTEDGOTO: {
		struct multijmp *jmp;
		buf += sprintf(buf, "%s", show_pseudo(insn->target));
		FOR_EACH_PTR(insn->multijmp_list, jmp) {
			buf += sprintf(buf, ", .L%p", jmp->target);
		} END_FOR_EACH_PTR(jmp);
		break;
	}

	case OP_PHISOURCE: {
		struct instruction *phi;
		buf += sprintf(buf, "%s <- %s    ", show_pseudo(insn->target), show_pseudo(insn->phi_src));
		FOR_EACH_PTR(insn->phi_users, phi) {
			buf += sprintf(buf, " (%s)", show_pseudo(phi->target));
		} END_FOR_EACH_PTR(phi);
		break;
	}

	case OP_PHI: {
		pseudo_t phi;
		const char *s = " <-";
		buf += sprintf(buf, "%s", show_pseudo(insn->target));
		FOR_EACH_PTR(insn->phi_list, phi) {
			buf += sprintf(buf, "%s %s", s, show_pseudo(phi));
			s = ",";
		} END_FOR_EACH_PTR(phi);
		break;
	}	
	case OP_LOAD: case OP_LNOP:
		buf += sprintf(buf, "%s <- %d[%s]", show_pseudo(insn->target), insn->offset, show_pseudo(insn->src));
		break;
	case OP_STORE: case OP_SNOP:
		buf += sprintf(buf, "%s -> %d[%s]", show_pseudo(insn->target), insn->offset, show_pseudo(insn->src));
		break;
	case OP_CALL: {
		struct pseudo *arg;
		if (insn->target && insn->target != VOID)
			buf += sprintf(buf, "%s <- ", show_pseudo(insn->target));
		buf += sprintf(buf, "%s", show_pseudo(insn->func));
		FOR_EACH_PTR(insn->arguments, arg) {
			buf += sprintf(buf, ", %s", show_pseudo(arg));
		} END_FOR_EACH_PTR(arg);
		break;
	}
	case OP_CAST:
	case OP_PTRCAST:
		buf += sprintf(buf, "%s <- (%d) %s",
			show_pseudo(insn->target),
			type_size(insn->orig_type),
			show_pseudo(insn->src));
		break;
	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		buf += sprintf(buf, "%s <- %s, %s", show_pseudo(insn->target), show_pseudo(insn->src1), show_pseudo(insn->src2));
		break;

	case OP_SEL:
		buf += sprintf(buf, "%s <- %s, %s, %s", show_pseudo(insn->target),
			show_pseudo(insn->src1), show_pseudo(insn->src2), show_pseudo(insn->src3));
		break;

	case OP_SLICE:
		buf += sprintf(buf, "%s <- %s, %d, %d", show_pseudo(insn->target), show_pseudo(insn->base), insn->from, insn->len);
		break;

	case OP_NOT: case OP_NEG:
		buf += sprintf(buf, "%s <- %s", show_pseudo(insn->target), show_pseudo(insn->src1));
		break;

	case OP_CONTEXT:
		buf += sprintf(buf, "%d", insn->increment);
		break;
	case OP_NOP:
		buf += sprintf(buf, "%s <- %s", show_pseudo(insn->target), show_pseudo(insn->src1));
		break;
	case OP_DEATHNOTE:
		buf += sprintf(buf, "%s", show_pseudo(insn->target));
		break;
	case OP_ASM:
		buf += sprintf(buf, "\"%s\"", insn->string);
		if (insn->outputs) {
			pseudo_t pseudo;
			buf += sprintf(buf, " (");
			FOR_EACH_PTR(insn->outputs, pseudo) {
				buf += sprintf(buf, " %s", show_pseudo(pseudo));
			} END_FOR_EACH_PTR(pseudo);
			buf += sprintf(buf, " ) <-");
		}
		if (insn->inputs) {
			pseudo_t pseudo;
			buf += sprintf(buf, " (");
			FOR_EACH_PTR(insn->inputs, pseudo) {
				buf += sprintf(buf, " %s", show_pseudo(pseudo));
			} END_FOR_EACH_PTR(pseudo);
			buf += sprintf(buf, " )");
		}
		break;
	default:
		break;
	}
	do { --buf; } while (*buf == ' ');
	*++buf = 0;
	printf("%s\n", buffer);
}

void show_bb(struct basic_block *bb)
{
	struct instruction *insn;

	printf(".L%p:\n", bb);
	if (verbose) {
		pseudo_t needs, defines;
		printf("%s:%d\n", stream_name(bb->pos.stream), bb->pos.line);

		FOR_EACH_PTR(bb->needs, needs) {
			struct instruction *def = needs->def;
			if (def->opcode != OP_PHI) {
				printf("  **uses %s (from .L%p)**\n", show_pseudo(needs), def->bb);
			} else {
				pseudo_t phi;
				const char *sep = " ";
				printf("  **uses %s (from", show_pseudo(needs));
				FOR_EACH_PTR(def->phi_list, phi) {
					if (phi == VOID)
						continue;
					printf("%s(%s:.L%p)", sep, show_pseudo(phi), phi->def->bb);
					sep = ", ";
				} END_FOR_EACH_PTR(phi);		
				printf(")**\n");
			}
		} END_FOR_EACH_PTR(needs);

		FOR_EACH_PTR(bb->defines, defines) {
			printf("  **defines %s **\n", show_pseudo(defines));
		} END_FOR_EACH_PTR(defines);

		if (bb->parents) {
			struct basic_block *from;
			FOR_EACH_PTR(bb->parents, from) {
				printf("  **from %p (%s:%d:%d)**\n", from,
					stream_name(from->pos.stream), from->pos.line, from->pos.pos);
			} END_FOR_EACH_PTR(from);
		}

		if (bb->children) {
			struct basic_block *to;
			FOR_EACH_PTR(bb->children, to) {
				printf("  **to %p (%s:%d:%d)**\n", to,
					stream_name(to->pos.stream), to->pos.line, to->pos.pos);
			} END_FOR_EACH_PTR(to);
		}
	}

	FOR_EACH_PTR(bb->insns, insn) {
		show_instruction(insn);
	} END_FOR_EACH_PTR(insn);
	if (!bb_terminated(bb))
		printf("\tEND\n");
}

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

	printf("%s:\n", show_ident(ep->name->ident));

	if (verbose) {
		printf("ep %p: %s\n", ep, show_ident(ep->name->ident));

		FOR_EACH_PTR(ep->syms, sym) {
			if (!sym->pseudo)
				continue;
			if (!sym->pseudo->users)
				continue;
			printf("   sym: %p %s\n", sym, show_ident(sym->ident));
			if (sym->ctype.modifiers & (MOD_EXTERN | MOD_STATIC | MOD_ADDRESSABLE))
				printf("\texternal visibility\n");
			show_symbol_usage(sym->pseudo);
		} END_FOR_EACH_PTR(sym);

		printf("\n");
	}

	FOR_EACH_PTR(ep->bbs, bb) {
		if (!bb)
			continue;
		if (!bb->parents && !bb->children && !bb->insns && verbose < 2)
			continue;
		show_bb(bb);
		printf("\n");
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
		bb = alloc_basic_block(ep, label->pos);
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
		struct instruction *br = alloc_instruction(OP_BR, 0);
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

static void remove_parent(struct basic_block *child, struct basic_block *parent)
{
	remove_bb_from_list(&child->parents, parent, 1);
	if (!child->parents)
		kill_bb(child);
}

/* Change a "switch" into a branch */
void insert_branch(struct basic_block *bb, struct instruction *jmp, struct basic_block *target)
{
	struct instruction *br, *old;
	struct basic_block *child;

	/* Remove the switch */
	old = delete_last_instruction(&bb->insns);
	assert(old == jmp);

	br = alloc_instruction(OP_BR, 0);
	br->bb = bb;
	br->bb_true = target;
	add_instruction(&bb->insns, br);

	FOR_EACH_PTR(bb->children, child) {
		if (child == target) {
			target = NULL;	/* Trigger just once */
			continue;
		}
		DELETE_CURRENT_PTR(child);
		remove_parent(child, bb);
	} END_FOR_EACH_PTR(child);
	PACK_PTR_LIST(&bb->children);
}
	

void insert_select(struct basic_block *bb, struct instruction *br, struct instruction *phi_node, pseudo_t true, pseudo_t false)
{
	pseudo_t target;
	struct instruction *select;

	/* Remove the 'br' */
	delete_last_instruction(&bb->insns);

	select = alloc_instruction(OP_SEL, phi_node->size);
	select->bb = bb;

	assert(br->cond);
	use_pseudo(br->cond, &select->src1);

	target = phi_node->target;
	assert(target->def == phi_node);
	select->target = target;
	target->def = select;

	use_pseudo(true, &select->src2);
	use_pseudo(false, &select->src3);

	add_instruction(&bb->insns, select);
	add_instruction(&bb->insns, br);
}

static inline int bb_empty(struct basic_block *bb)
{
	return !bb->insns;
}

/* Add a label to the currently active block, return new active block */
static struct basic_block * add_label(struct entrypoint *ep, struct symbol *label)
{
	struct basic_block *bb = label->bb_target;

	if (bb) {
		set_activeblock(ep, bb);
		return bb;
	}
	bb = ep->active;
	if (!bb_reachable(bb) || !bb_empty(bb)) {
		bb = alloc_basic_block(ep, label->pos);
		set_activeblock(ep, bb);
	}
	label->bb_target = bb;
	return bb;
}

static void add_branch(struct entrypoint *ep, struct expression *expr, pseudo_t cond, struct basic_block *bb_true, struct basic_block *bb_false)
{
	struct basic_block *bb = ep->active;
	struct instruction *br;

	if (bb_reachable(bb)) {
       		br = alloc_instruction(OP_BR, 0);
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
pseudo_t alloc_pseudo(struct instruction *def)
{
	static int nr = 0;
	struct pseudo * pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_REG;
	pseudo->nr = ++nr;
	pseudo->def = def;
	return pseudo;
}

static void clear_symbol_pseudos(struct entrypoint *ep)
{
	struct symbol *sym;

	FOR_EACH_PTR(ep->accesses, sym) {
		sym->pseudo = NULL;
	} END_FOR_EACH_PTR(sym);
}

static pseudo_t symbol_pseudo(struct entrypoint *ep, struct symbol *sym)
{
	pseudo_t pseudo;

	if (!sym)
		return VOID;

	pseudo = sym->pseudo;
	if (!pseudo) {
		pseudo = __alloc_pseudo(0);
		pseudo->type = PSEUDO_SYM;
		pseudo->sym = sym;
		pseudo->ident = sym->ident;
		sym->pseudo = pseudo;
		add_symbol(&ep->accesses, sym);
	}
	/* Symbol pseudos have neither nr, usage nor def */
	return pseudo;
}

pseudo_t value_pseudo(long long val)
{
#define MAX_VAL_HASH 64
	static struct pseudo_list *prev[MAX_VAL_HASH];
	int hash = val & (MAX_VAL_HASH-1);
	struct pseudo_list **list = prev + hash;
	pseudo_t pseudo;

	FOR_EACH_PTR(*list, pseudo) {
		if (pseudo->value == val)
			return pseudo;
	} END_FOR_EACH_PTR(pseudo);

	pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_VAL;
	pseudo->value = val;
	add_pseudo(list, pseudo);

	/* Value pseudos have neither nr, usage nor def */
	return pseudo;
}

static pseudo_t argument_pseudo(struct entrypoint *ep, int nr)
{
	pseudo_t pseudo = __alloc_pseudo(0);
	pseudo->type = PSEUDO_ARG;
	pseudo->nr = nr;
	pseudo->def = ep->entry;
	/* Argument pseudos have neither usage nor def */
	return pseudo;
}

pseudo_t alloc_phi(struct basic_block *source, pseudo_t pseudo, int size)
{
	struct instruction *insn = alloc_instruction(OP_PHISOURCE, size);
	pseudo_t phi = __alloc_pseudo(0);
	static int nr = 0;

	phi->type = PSEUDO_PHI;
	phi->nr = ++nr;
	phi->def = insn;

	use_pseudo(pseudo, &insn->phi_src);
	insn->bb = source;
	insn->target = phi;
	add_instruction(&source->insns, insn);
	return phi;
}

/*
 * We carry the "access_data" structure around for any accesses,
 * which simplifies things a lot. It contains all the access
 * information in one place.
 */
struct access_data {
	struct symbol *result_type;	// result ctype
	struct symbol *source_type;	// source ctype
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
		linearize_one_symbol(ep, addr->symbol);
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

static struct symbol *base_type(struct symbol *sym)
{
	struct symbol *base = sym;

	if (sym) {
		if (sym->type == SYM_NODE)
			base = base->ctype.base_type;
		if (base->type == SYM_BITFIELD)
			return base->ctype.base_type;
	}
	return sym;
}

static int linearize_address_gen(struct entrypoint *ep,
	struct expression *expr,
	struct access_data *ad)
{
	struct symbol *ctype = expr->ctype;

	if (!ctype)
		return 0;
	ad->pos = expr->pos;
	ad->result_type = ctype;
	ad->source_type = base_type(ctype);
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
	if (0 && new)
		return new;

	insn = alloc_typed_instruction(OP_LOAD, ad->source_type);
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
		struct instruction *store = alloc_typed_instruction(OP_STORE, ad->source_type);
		store->offset = ad->offset;
		use_pseudo(value, &store->target);
		use_pseudo(ad->address, &store->src);
		add_one_insn(ep, store);
	}
}

static pseudo_t linearize_store_gen(struct entrypoint *ep,
		pseudo_t value,
		struct access_data *ad)
{
	pseudo_t store = value;

	if (type_size(ad->source_type) != type_size(ad->result_type)) {
		pseudo_t orig = add_load(ep, ad);
		int shift = ad->bit_offset;
		unsigned long long mask = (1ULL << ad->bit_size)-1;

		if (shift) {
			store = add_binary_op(ep, ad->source_type, OP_SHL, value, value_pseudo(shift));
			mask <<= shift;
		}
		orig = add_binary_op(ep, ad->source_type, OP_AND, orig, value_pseudo(~mask));
		store = add_binary_op(ep, ad->source_type, OP_OR, orig, store);
	}
	add_store(ep, ad, store);
	return value;
}

static pseudo_t add_binary_op(struct entrypoint *ep, struct symbol *ctype, int op, pseudo_t left, pseudo_t right)
{
	struct instruction *insn = alloc_typed_instruction(op, ctype);
	pseudo_t target = alloc_pseudo(insn);
	insn->target = target;
	use_pseudo(left, &insn->src1);
	use_pseudo(right, &insn->src2);
	add_one_insn(ep, insn);
	return target;
}

static pseudo_t add_setval(struct entrypoint *ep, struct symbol *ctype, struct expression *val)
{
	struct instruction *insn = alloc_typed_instruction(OP_SETVAL, ctype);
	pseudo_t target = alloc_pseudo(insn);
	insn->target = target;
	insn->val = val;
	if (!val) {
		pseudo_t addr = symbol_pseudo(ep, ctype);
		use_pseudo(addr, &insn->symbol);
		insn->size = bits_in_pointer;
	}
	add_one_insn(ep, insn);
	return target;
}

static pseudo_t linearize_load_gen(struct entrypoint *ep, struct access_data *ad)
{
	pseudo_t new = add_load(ep, ad);

	if (ad->bit_offset) {
		pseudo_t shift = value_pseudo(ad->bit_offset);
		pseudo_t newval = add_binary_op(ep, ad->source_type, OP_SHR, new, shift);
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
	one = value_pseudo(expr->op_value);
	new = add_binary_op(ep, expr->ctype, op, old, one);
	linearize_store_gen(ep, new, &ad);
	finish_address_gen(ep, &ad);
	return postop ? old : new;
}

static pseudo_t add_uniop(struct entrypoint *ep, struct expression *expr, int op, pseudo_t src)
{
	struct instruction *insn = alloc_typed_instruction(op, expr->ctype);
	pseudo_t new = alloc_pseudo(insn);

	insn->target = new;
	use_pseudo(src, &insn->src1);
	add_one_insn(ep, insn);
	return new;
}

static pseudo_t linearize_slice(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t pre = linearize_expression(ep, expr->base);
	struct instruction *insn = alloc_typed_instruction(OP_SLICE, expr->ctype);
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
	struct instruction *insn = alloc_typed_instruction(OP_CALL, expr->ctype);
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
		insn = alloc_instruction(OP_CONTEXT, 0);
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
	struct instruction *insn;

	true = linearize_expression(ep, expr->cond_true);
	false = linearize_expression(ep, expr->cond_false);
	cond = linearize_expression(ep, expr->conditional);

	insn = alloc_typed_instruction(OP_SEL, expr->ctype);
	if (!expr->cond_true)
		true = cond;
	use_pseudo(cond, &insn->src1);
	use_pseudo(true, &insn->src2);
	use_pseudo(false, &insn->src3);

	res = alloc_pseudo(insn);
	insn->target = res;
	add_one_insn(ep, insn);
	return res;
}

static pseudo_t add_join_conditional(struct entrypoint *ep, struct expression *expr,
				     pseudo_t phi1, pseudo_t phi2)
{
	pseudo_t target;
	struct instruction *phi_node;

	if (phi1 == VOID)
		return phi2;
	if (phi2 == VOID)
		return phi1;

	phi_node = alloc_typed_instruction(OP_PHI, expr->ctype);
	use_pseudo(phi1, add_pseudo(&phi_node->phi_list, phi1));
	use_pseudo(phi2, add_pseudo(&phi_node->phi_list, phi2));
	phi_node->target = target = alloc_pseudo(phi_node);
	add_one_insn(ep, phi_node);
	return target;
}	

static pseudo_t linearize_short_conditional(struct entrypoint *ep, struct expression *expr,
					    struct expression *cond,
					    struct expression *expr_false)
{
	pseudo_t src1, src2;
	struct basic_block *bb_false = alloc_basic_block(ep, expr_false->pos);
	struct basic_block *merge = alloc_basic_block(ep, expr->pos);
	pseudo_t phi1, phi2;
	int size = type_size(expr->ctype);

	src1 = linearize_expression(ep, cond);
	phi1 = alloc_phi(ep->active, src1, size);
	add_branch(ep, expr, src1, merge, bb_false);

	set_activeblock(ep, bb_false);
	src2 = linearize_expression(ep, expr_false);
	phi2 = alloc_phi(ep->active, src2, size);
	set_activeblock(ep, merge);

	return add_join_conditional(ep, expr, phi1, phi2);
}

static pseudo_t linearize_conditional(struct entrypoint *ep, struct expression *expr,
				      struct expression *cond,
				      struct expression *expr_true,
				      struct expression *expr_false)
{
	pseudo_t src1, src2;
	pseudo_t phi1, phi2;
	struct basic_block *bb_true = alloc_basic_block(ep, expr_true->pos);
	struct basic_block *bb_false = alloc_basic_block(ep, expr_false->pos);
	struct basic_block *merge = alloc_basic_block(ep, expr->pos);
	int size = type_size(expr->ctype);

	linearize_cond_branch(ep, cond, bb_true, bb_false);

	set_activeblock(ep, bb_true);
	src1 = linearize_expression(ep, expr_true);
	phi1 = alloc_phi(ep->active, src1, size);
	add_goto(ep, merge); 

	set_activeblock(ep, bb_false);
	src2 = linearize_expression(ep, expr_false);
	phi2 = alloc_phi(ep->active, src2, size);
	set_activeblock(ep, merge);

	return add_join_conditional(ep, expr, phi1, phi2);
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
	struct basic_block *next = alloc_basic_block(ep, expr->pos);

	if (expr->op == SPECIAL_LOGICAL_OR)
		linearize_cond_branch(ep, expr->left, bb_true, next);
	else
		linearize_cond_branch(ep, expr->left, next, bb_false);
	set_activeblock(ep, next);
	linearize_cond_branch(ep, expr->right, bb_true, bb_false);
	return VOID;
}

/*
 * Casts to pointers are "less safe" than other casts, since
 * they imply type-unsafe accesses. "void *" is a special
 * case, since you can't access through it anyway without another
 * cast.
 */
static struct instruction *alloc_cast_instruction(struct symbol *ctype)
{
	int opcode = OP_CAST;
	struct symbol *base = ctype;

	if (base->type == SYM_NODE)
		base = base->ctype.base_type;
	if (base->type == SYM_PTR) {
		base = base->ctype.base_type;
		if (base != &void_ctype)
			opcode = OP_PTRCAST;
	}
	return alloc_typed_instruction(opcode, ctype);
}

pseudo_t linearize_cast(struct entrypoint *ep, struct expression *expr)
{
	pseudo_t src, result;
	struct instruction *insn;

	src = linearize_expression(ep, expr->cast_expression);
	if (src == VOID)
		return VOID;
	if (!expr->ctype)
		return VOID;
	if (expr->ctype->bit_size < 0)
		return VOID;

	insn = alloc_cast_instruction(expr->ctype);
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
	ad->source_type = base_type(init_expr->ctype);
	ad->result_type = init_expr->ctype;
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
		ad->source_type = base_type(initializer->ctype);
		ad->result_type = initializer->ctype;
		linearize_store_gen(ep, value, ad);
	}
	}

	return VOID;
}

void linearize_argument(struct entrypoint *ep, struct symbol *arg, int nr)
{
	struct access_data ad = { NULL, };

	ad.source_type = arg;
	ad.result_type = arg;
	ad.address = symbol_pseudo(ep, arg);
	linearize_store_gen(ep, argument_pseudo(ep, nr), &ad);
	finish_address_gen(ep, &ad);
}

pseudo_t linearize_expression(struct entrypoint *ep, struct expression *expr)
{
	if (!expr)
		return VOID;

	switch (expr->type) {
	case EXPR_SYMBOL:
		linearize_one_symbol(ep, expr->symbol);
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

	if (!sym || !sym->initializer || sym->initialized)
		return;

	/* We need to output these puppies some day too.. */
	if (sym->ctype.modifiers & (MOD_STATIC | MOD_TOPLEVEL))
		return;

	sym->initialized = 1;
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
		struct basic_block *bb = add_label(ep, ret);
		struct instruction *phi_node = first_instruction(bb->insns);

		if (!phi_node)
			return pseudo;

		if (pseudo_list_size(phi_node->phi_list)==1) {
			pseudo = first_pseudo(phi_node->phi_list);
			assert(pseudo->type == PSEUDO_PHI);
			return pseudo->def->src1;
		}
		return phi_node->target;
	}
	return pseudo;
}

pseudo_t linearize_internal(struct entrypoint *ep, struct statement *stmt)
{
	struct instruction *insn = alloc_instruction(OP_CONTEXT, 0);
	struct expression *expr = stmt->expression;
	int value = 0;

	if (expr->type == EXPR_VALUE)
		value = expr->value;

	insn->increment = value;
	add_one_insn(ep, insn);
	return VOID;
}

static void add_asm_input(struct entrypoint *ep, struct instruction *insn, struct expression *expr)
{
	pseudo_t pseudo = linearize_expression(ep, expr);

	use_pseudo(pseudo, add_pseudo(&insn->inputs, pseudo));
}

static void add_asm_output(struct entrypoint *ep, struct instruction *insn, struct expression *expr)
{
	struct access_data ad = { NULL, };
	pseudo_t pseudo = alloc_pseudo(insn);

	if (!linearize_address_gen(ep, expr, &ad))
		return;
	linearize_store_gen(ep, pseudo, &ad);
	finish_address_gen(ep, &ad);
	add_pseudo(&insn->outputs, pseudo);
}

pseudo_t linearize_asm_statement(struct entrypoint *ep, struct statement *stmt)
{
	int even_odd;
	struct expression *expr;
	struct instruction *insn;

	insn = alloc_instruction(OP_ASM, 0);
	expr = stmt->asm_string;
	if (!expr || expr->type != EXPR_STRING) {
		warning(stmt->pos, "expected string in inline asm");
		return VOID;
	}
	insn->string = expr->string->data;

	/* Gather the inputs.. */
	even_odd = 0;
	FOR_EACH_PTR(stmt->asm_inputs, expr) {
		even_odd = 1 - even_odd;

		/* FIXME! We ignore the constraints for now.. */
		if (even_odd)
			continue;
		add_asm_input(ep, insn, expr);
	} END_FOR_EACH_PTR(expr);

	add_one_insn(ep, insn);

	/* Assign the outputs */
	even_odd = 0;
	FOR_EACH_PTR(stmt->asm_outputs, expr) {
		even_odd = 1 - even_odd;

		/* FIXME! We ignore the constraints for now.. */
		if (even_odd)
			continue;
		add_asm_output(ep, insn, expr);
	} END_FOR_EACH_PTR(expr);

	return VOID;
}

pseudo_t linearize_statement(struct entrypoint *ep, struct statement *stmt)
{
	struct basic_block *bb;

	if (!stmt)
		return VOID;

	bb = ep->active;
	if (bb && !bb->insns)
		bb->pos = stmt->pos;

	switch (stmt->type) {
	case STMT_NONE:
		break;

	case STMT_INTERNAL:
		return linearize_internal(ep, stmt);

	case STMT_EXPRESSION:
		return linearize_expression(ep, stmt->expression);

	case STMT_ASM:
		return linearize_asm_statement(ep, stmt);

	case STMT_RETURN: {
		struct expression *expr = stmt->expression;
		struct basic_block *bb_return = get_bound_block(ep, stmt->ret_target);
		struct basic_block *active;
		pseudo_t src = linearize_expression(ep, expr);
		active = ep->active;
		if (active && src != &void_pseudo) {
			struct instruction *phi_node = first_instruction(bb_return->insns);
			pseudo_t phi;
			if (!phi_node) {
				phi_node = alloc_typed_instruction(OP_PHI, expr->ctype);
				phi_node->target = alloc_pseudo(phi_node);
				phi_node->bb = bb_return;
				add_instruction(&bb_return->insns, phi_node);
			}
			phi = alloc_phi(active, src, type_size(expr->ctype));
			phi->ident = &return_ident;
			use_pseudo(phi, add_pseudo(&phi_node->phi_list, phi));
		}
		add_goto(ep, bb_return);
		return VOID;
	}

	case STMT_CASE: {
		add_label(ep, stmt->case_label);
		linearize_statement(ep, stmt->case_statement);
		break;
	}

	case STMT_LABEL: {
		struct symbol *label = stmt->label_identifier;

		if (label->used) {
			add_label(ep, label);
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
		goto_ins = alloc_instruction(OP_COMPUTEDGOTO, 0);
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

		bb_true = alloc_basic_block(ep, stmt->pos);
		bb_false = endif = alloc_basic_block(ep, stmt->pos);

 		linearize_cond_branch(ep, cond, bb_true, bb_false);

		set_activeblock(ep, bb_true);
 		linearize_statement(ep, stmt->if_true);
 
 		if (stmt->if_false) {
			endif = alloc_basic_block(ep, stmt->pos);
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
		struct basic_block *switch_end = alloc_basic_block(ep, stmt->pos);
		struct basic_block *active, *default_case;
		struct multijmp *jmp;
		pseudo_t pseudo;

		pseudo = linearize_expression(ep, stmt->switch_expression);

		active = ep->active;
		if (!bb_reachable(active))
			break;

		switch_ins = alloc_instruction(OP_SWITCH, 0);
		use_pseudo(pseudo, &switch_ins->cond);
		add_one_insn(ep, switch_ins);
		finish_block(ep);

		default_case = NULL;
		FOR_EACH_PTR(stmt->switch_case->symbol_list, sym) {
			struct statement *case_stmt = sym->stmt;
			struct basic_block *bb_case = get_bound_block(ep, sym);

			if (!case_stmt->case_expression) {
				default_case = bb_case;
				continue;
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

		if (!default_case)
			default_case = switch_end;

		jmp = alloc_multijmp(default_case, 1, 0);
		add_multijmp(&switch_ins->multijmp_list, jmp);
		add_bb(&default_case->parents, active);
		add_bb(&active->children, default_case);

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

 		loop_body = loop_top = alloc_basic_block(ep, stmt->pos);
 		loop_continue = alloc_basic_block(ep, stmt->pos);
 		loop_end = alloc_basic_block(ep, stmt->pos);
 
		if (pre_condition == post_condition) {
			loop_top = alloc_basic_block(ep, stmt->pos);
			set_activeblock(ep, loop_top);
		}

		if (pre_condition) 
 			linearize_cond_branch(ep, pre_condition, loop_body, loop_end);

		bind_label(stmt->iterator_continue, loop_continue, stmt->pos);
		bind_label(stmt->iterator_break, loop_end, stmt->pos);

		set_activeblock(ep, loop_body);
		linearize_statement(ep, statement);
		add_goto(ep, loop_continue);

		set_activeblock(ep, loop_continue);
		linearize_statement(ep, post_statement);
		if (!post_condition || pre_condition == post_condition)
			add_goto(ep, loop_top);
		else
 			linearize_cond_branch(ep, post_condition, loop_top, loop_end);
		set_activeblock(ep, loop_end);
		break;
	}

	default:
		break;
	}
	return VOID;
}

static struct entrypoint *linearize_fn(struct symbol *sym, struct symbol *base_type)
{
	struct entrypoint *ep;
	struct basic_block *bb;
	struct symbol *arg;
	struct instruction *entry;
	pseudo_t result;
	int i;

	if (!base_type->stmt)
		return NULL;

	ep = alloc_entrypoint();
	bb = alloc_basic_block(ep, sym->pos);
	
	ep->name = sym;
	set_activeblock(ep, bb);

	entry = alloc_instruction(OP_ENTRY, 0);
	add_one_insn(ep, entry);
	ep->entry = entry;

	concat_symbol_list(base_type->arguments, &ep->syms);

	/* FIXME!! We should do something else about varargs.. */
	i = 0;
	FOR_EACH_PTR(base_type->arguments, arg) {
		linearize_argument(ep, arg, ++i);
	} END_FOR_EACH_PTR(arg);

	result = linearize_statement(ep, base_type->stmt);
	if (bb_reachable(ep->active) && !bb_terminated(ep->active)) {
		struct symbol *ret_type = base_type->ctype.base_type;
		struct instruction *insn = alloc_typed_instruction(OP_RET, ret_type);

		if (type_size(ret_type) > 0)
			use_pseudo(result, &insn->src);
		add_one_insn(ep, insn);
	}

	/*
	 * Do trivial flow simplification - branches to
	 * branches, kill dead basicblocks etc
	 */
	kill_unreachable_bbs(ep);

	/*
	 * Turn symbols into pseudos
	 */
	simplify_symbol_usage(ep);

repeat:
	/*
	 * Remove trivial instructions, and try to CSE
	 * the rest.
	 */
	do {
		cleanup_and_cse(ep);
		pack_basic_blocks(ep);
	} while (repeat_phase & REPEAT_CSE);

	kill_unreachable_bbs(ep);
	vrfy_flow(ep);

	/* Cleanup */
	clear_symbol_pseudos(ep);

	/* And track pseudo register usage */
	track_pseudo_liveness(ep);

	/*
	 * Some flow optimizations can only effectively
	 * be done when we've done liveness analysis. But
	 * if they trigger, we need to start all over
	 * again
	 */
	if (simplify_flow(ep)) {
		clear_liveness(ep);
		goto repeat;
	}

	/* Finally, add deathnotes to pseudos now that we have them */
	track_pseudo_death(ep);

	return ep;
}

struct entrypoint *linearize_symbol(struct symbol *sym)
{
	struct symbol *base_type;

	if (!sym)
		return NULL;
	base_type = sym->ctype.base_type;
	if (!base_type)
		return NULL;
	if (base_type->type == SYM_FN)
		return linearize_fn(sym, base_type);
	return NULL;
}
