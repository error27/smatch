/*
 * Example of how to write a compiler with sparse
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"
#include "storage.h"

struct hardreg {
	const char *name;
	struct pseudo_list *contains;
	unsigned busy:16,
		 used:1;
};

#define TAG_DEAD 1
#define TAG_DIRTY 2

static void output_bb(struct basic_block *bb, unsigned long generation);

/*
 * We only know about the caller-clobbered registers
 * right now.
 */
static struct hardreg hardregs[] = {
	{ .name = "%eax" },
	{ .name = "%edx" },
	{ .name = "%ecx" },
	{ .name = "%ebx" },
	{ .name = "%esi" },
	{ .name = "%edi" },
};
#define REGNO (sizeof(hardregs)/sizeof(struct hardreg))

struct bb_state {
	struct basic_block *bb;
	unsigned long stack_offset;
	struct storage_hash_list *inputs;
	struct storage_hash_list *outputs;
	struct storage_hash_list *internal;
};

static struct storage_hash *find_storage_hash(pseudo_t pseudo, struct storage_hash_list *list)
{
	struct storage_hash *entry;
	FOR_EACH_PTR(list, entry) {
		if (entry->pseudo == pseudo)
			return entry;
	} END_FOR_EACH_PTR(entry);
	return NULL;
}

static struct storage_hash *find_or_create_hash(pseudo_t pseudo, struct storage_hash_list **listp)
{
	struct storage_hash *entry;

	entry = find_storage_hash(pseudo, *listp);
	if (!entry) {
		entry = alloc_storage_hash(alloc_storage());
		entry->pseudo = pseudo;
		add_ptr_list(listp, entry);
	}
	return entry;
}

/* Eventually we should just build it up in memory */
static void output_line(struct bb_state *state, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

static void output_label(struct bb_state *state, const char *fmt, ...)
{
	static char buffer[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	output_line(state, "%s:\n", buffer);
}

static void output_insn(struct bb_state *state, const char *fmt, ...)
{
	static char buffer[512];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	output_line(state, "\t%s\n", buffer);
}

static void output_comment(struct bb_state *state, const char *fmt, ...)
{
	static char buffer[512];
	va_list args;

	if (!verbose)
		return;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	output_line(state, "\t# %s\n", buffer);
}

static const char *show_memop(struct storage *storage)
{
	static char buffer[1000];
	switch (storage->type) {
	case REG_ARG:
		sprintf(buffer, "%d(FP)", storage->regno * 4);
		break;
	case REG_STACK:
		sprintf(buffer, "%d(SP)", storage->offset);
		break;
	case REG_REG:
		return hardregs[storage->regno].name;
	default:
		return show_storage(storage);
	}
	return buffer;
}

static void alloc_stack(struct bb_state *state, struct storage *storage)
{
	storage->type = REG_STACK;
	storage->offset = state->stack_offset;
	state->stack_offset += 4;
}

/*
 * Can we re-generate the pseudo, so that we don't need to
 * flush it to memory? We can regenerate:
 *  - immediates and symbol addresses
 *  - pseudos we got as input in non-registers
 *  - pseudos we've already saved off earlier..
 */
static int can_regenerate(struct bb_state *state, pseudo_t pseudo)
{
	struct storage_hash *in;

	switch (pseudo->type) {
	case PSEUDO_VAL:
	case PSEUDO_SYM:
		return 1;

	default:
		in = find_storage_hash(pseudo, state->inputs);
		if (in && in->storage->type != REG_REG)
			return 1;
		in = find_storage_hash(pseudo, state->internal);
		if (in)
			return 1;
	}
	return 0;
}

static void flush_one_pseudo(struct bb_state *state, struct hardreg *hardreg, pseudo_t pseudo)
{
	struct storage_hash *out;
	struct storage *storage;

	if (can_regenerate(state, pseudo))
		return;

	output_comment(state, "flushing %s from %s", show_pseudo(pseudo), hardreg->name);
	out = find_storage_hash(pseudo, state->internal);
	if (!out) {
		out = find_storage_hash(pseudo, state->outputs);
		if (!out)
			out = find_or_create_hash(pseudo, &state->internal);
	}
	storage = out->storage;
	switch (storage->type) {
	default:
		/*
		 * Aieee - the next user wants it in a register, but we
		 * need to flush it to memory in between. Which means that
		 * we need to allocate an internal one, dammit..
		 */
		out = find_or_create_hash(pseudo, &state->internal);
		storage = out->storage;
		/* Fall through */
	case REG_UDEF:
		alloc_stack(state, storage);
		/* Fall through */
	case REG_STACK:
		output_insn(state, "movl %s,%s", hardreg->name, show_memop(storage));
		break;
	}
}

/* Flush a hardreg out to the storage it has.. */
static void flush_reg(struct bb_state *state, struct hardreg *hardreg)
{
	pseudo_t pseudo;

	if (!hardreg->busy)
		return;
	hardreg->busy = 0;
	hardreg->used = 1;
	FOR_EACH_PTR(hardreg->contains, pseudo) {
		if (CURRENT_TAG(pseudo) & TAG_DEAD)
			continue;
		if (!(CURRENT_TAG(pseudo) & TAG_DIRTY))
			continue;
		flush_one_pseudo(state, hardreg, pseudo);
	} END_FOR_EACH_PTR(pseudo);
	free_ptr_list(&hardreg->contains);
}

/* Fill a hardreg with the pseudo it has */
static struct hardreg *fill_reg(struct bb_state *state, struct hardreg *hardreg, pseudo_t pseudo)
{
	struct storage_hash *src;
	struct instruction *def;

	switch (pseudo->type) {
	case PSEUDO_VAL:
		output_insn(state, "movl $%lld,%s", pseudo->value, hardreg->name);
		break;
	case PSEUDO_SYM:
		output_insn(state, "movl $<%s>,%s", show_pseudo(pseudo), hardreg->name);
		break;
	case PSEUDO_ARG:
	case PSEUDO_REG:
		def = pseudo->def;
		if (def->opcode == OP_SETVAL) {
			output_insn(state, "movl $<%s>,%s", show_pseudo(def->symbol), hardreg->name);
			break;
		}
		src = find_storage_hash(pseudo, state->internal);
		if (!src) {
			src = find_storage_hash(pseudo, state->inputs);
			if (!src) {
				src = find_storage_hash(pseudo, state->outputs);
				/* Undefined? Screw it! */
				if (!src) {
					output_insn(state, "undef %s ??", show_pseudo(pseudo));
					break;
				}

				/*
				 * If we found output storage, it had better be local stack
				 * that we flushed to earlier..
				 */
				if (src->storage->type != REG_STACK) {
					output_insn(state, "fill_reg on undef storage %s ??", show_pseudo(pseudo));
					break;
				}
			}
		}

		/*
		 * Incoming pseudo with out any pre-set storage allocation?
		 * We can make up our own, and obviously prefer to get it
		 * in the register we already selected (if it hasn't been
		 * used yet).
		 */
		if (src->storage->type == REG_UDEF) {
			if (!hardreg->used) {
				src->storage->type = REG_REG;
				src->storage->regno = hardreg - hardregs;
				break;
			}
			alloc_stack(state, src->storage);
		}
		output_insn(state, "mov.%d %s,%s", 32, show_memop(src->storage), hardreg->name);
		break;
	default:
		output_insn(state, "reload %s from %s", hardreg->name, show_pseudo(pseudo));
		break;
	}
	return hardreg;
}

static void add_pseudo_reg(struct bb_state *state, pseudo_t pseudo, struct hardreg *reg)
{
	output_comment(state, "added pseudo %s to reg %s", show_pseudo(pseudo), reg->name);
	add_ptr_list_tag(&reg->contains, pseudo, TAG_DIRTY);
	reg->busy++;
}

static int last_reg;

static struct hardreg *preferred_reg(struct bb_state *state, pseudo_t target)
{
	struct storage_hash *dst;

	dst = find_storage_hash(target, state->outputs);
	if (dst) {
		struct storage *storage = dst->storage;
		if (storage->type == REG_REG)
			return hardregs + storage->regno;
	}
	return NULL;
}

static struct hardreg *target_reg(struct bb_state *state, pseudo_t pseudo, pseudo_t target)
{
	int i;
	struct hardreg *reg;

	/* First, see if we have a preferred target register.. */
	reg = preferred_reg(state, target);
	if (reg && !reg->busy)
		goto found;

	i = last_reg+1;
	if (i >= REGNO)
		i = 0;
	last_reg = i;
	reg = hardregs + i;
	flush_reg(state, reg);

found:
	add_pseudo_reg(state, pseudo, reg);
	return reg;
}

static struct hardreg *getreg(struct bb_state *state, pseudo_t pseudo, pseudo_t target)
{
	int i;
	struct hardreg *reg;

	for (i = 0; i < REGNO; i++) {
		pseudo_t p;

		reg = hardregs + i;
		FOR_EACH_PTR(reg->contains, p) {
			if (p == pseudo) {
				last_reg = i;
				output_comment(state, "found pseudo %s in reg %s", show_pseudo(pseudo), reg->name);
				return reg;
			}
		} END_FOR_EACH_PTR(p);
	}
	reg = target_reg(state, pseudo, target);
	return fill_reg(state, reg, pseudo);
}

static struct hardreg *copy_reg(struct bb_state *state, struct hardreg *src, pseudo_t target)
{
	int i;
	struct hardreg *reg;

	if (!src->busy)
		return src;

	reg = preferred_reg(state, target);
	if (reg && !reg->busy) {
		output_comment(state, "copying %s to preferred target %s", show_pseudo(target), reg->name);
		output_insn(state, "movl %s,%s", src->name, reg->name);
		return reg;
	}

	for (i = 0; i < REGNO; i++) {
		struct hardreg *reg = hardregs + i;
		if (!reg->busy) {
			output_comment(state, "copying %s to %s", show_pseudo(target), reg->name);
			output_insn(state, "movl %s,%s", src->name, reg->name);
			return reg;
		}
	}

	flush_reg(state, src);
	return src;
}

static const char *address(struct bb_state *state, struct instruction *memop)
{
	struct symbol *sym;
	struct hardreg *base;
	static char buffer[100];
	pseudo_t addr = memop->src;

	switch(addr->type) {
	case PSEUDO_SYM:
		sym = addr->sym;
		if (sym->ctype.modifiers & MOD_NONLOCAL) {
			sprintf(buffer, "%s+%d", show_ident(sym->ident), memop->offset);
			return buffer;
		}
		sprintf(buffer, "%d+%s(SP)", memop->offset, show_ident(sym->ident));
		return buffer;
	default:
		base = getreg(state, addr, NULL);
		sprintf(buffer, "%d(%s)", memop->offset, base->name);
		return buffer;
	}
}

static const char *reg_or_imm(struct bb_state *state, pseudo_t pseudo)
{
	switch(pseudo->type) {
	case PSEUDO_VAL:
		return show_pseudo(pseudo);
	default:
		return getreg(state, pseudo, NULL)->name;
	}
}

static void generate_store(struct instruction *insn, struct bb_state *state)
{
	output_insn(state, "mov.%d %s,%s", insn->size, reg_or_imm(state, insn->target), address(state, insn));
}

static void generate_load(struct instruction *insn, struct bb_state *state)
{
	const char *input = address(state, insn);
	output_insn(state, "mov.%d %s,%s", insn->size, input, target_reg(state, insn->target, NULL)->name);
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

static void generate_binop(struct bb_state *state, struct instruction *insn)
{
	const char *op = opcodes[insn->opcode];
	struct hardreg *src = getreg(state, insn->src1, insn->target);
	struct hardreg *dst = copy_reg(state, src, insn->target);

	output_insn(state, "%s.%d %s,%s", op, insn->size, reg_or_imm(state, insn->src2), dst->name);
	add_pseudo_reg(state, insn->target, dst);
}

static void mark_pseudo_dead(struct bb_state *state, pseudo_t pseudo)
{
	int i;

	for (i = 0; i < REGNO; i++) {
		pseudo_t p;
		struct hardreg *reg = hardregs + i;
		FOR_EACH_PTR(reg->contains, p) {
			if (p != pseudo)
				continue;
			if (CURRENT_TAG(p) & TAG_DEAD)
				continue;
			output_comment(state, "marking pseudo %s in reg %s dead", show_pseudo(pseudo), reg->name);
			TAG_CURRENT(p, TAG_DEAD);
			reg->busy--;
		} END_FOR_EACH_PTR(p);
	}
}

/*
 * A PHI source can define a pseudo that we already
 * have in another register. We need to invalidate the
 * old register so that we don't end up with the same
 * pseudo in "two places".
 */
static void remove_pseudo_reg(struct bb_state *state, pseudo_t pseudo)
{
	int i;

	output_comment(state, "pseudo %s died", show_pseudo(pseudo));
	for (i = 0; i < REGNO; i++) {
		struct hardreg *reg = hardregs + i;
		if (remove_pseudo(&reg->contains, pseudo))
			output_comment(state, "removed pseudo %s from reg %s", show_pseudo(pseudo), reg->name);
	}
}

static void generate_phisource(struct instruction *insn, struct bb_state *state)
{
	struct instruction *user;
	struct hardreg *reg;

	/* Mark all the target pseudos dead first */
	FOR_EACH_PTR(insn->phi_users, user) {
		mark_pseudo_dead(state, user->target);
	} END_FOR_EACH_PTR(user);

	reg = NULL;
	FOR_EACH_PTR(insn->phi_users, user) {
		if (!reg)
			reg = getreg(state, insn->phi_src, user->target);
		remove_pseudo_reg(state, user->target);
		add_pseudo_reg(state, user->target, reg);
	} END_FOR_EACH_PTR(user);
}

static void generate_output_storage(struct bb_state *state);

static void generate_branch(struct bb_state *state, struct instruction *br)
{
	if (br->cond) {
		struct hardreg *reg = getreg(state, br->cond, NULL);
		output_insn(state, "testl %s,%s", reg->name, reg->name);
	}
	generate_output_storage(state);
	if (br->cond)
		output_insn(state, "je .L%p", br->bb_false);
	output_insn(state, "jmp .L%p", br->bb_true);
}

static void generate_ret(struct bb_state *state, struct instruction *ret)
{
	if (ret->src && ret->src != VOID) {
		struct hardreg *wants = hardregs+0;
		struct hardreg *reg = getreg(state, ret->src, NULL);
		if (reg != wants)
			output_insn(state, "movl %s,%s", reg->name, wants->name);
	}
	output_insn(state, "ret");
}

static void generate_one_insn(struct instruction *insn, struct bb_state *state)
{
	if (verbose)
		output_comment(state, "%s", show_instruction(insn));

	switch (insn->opcode) {
	case OP_ENTRY: {
		struct symbol *sym = insn->bb->ep->name;
		const char *name = show_ident(sym->ident);
		if (sym->ctype.modifiers & MOD_STATIC)
			printf("\n\n%s:\n", name);
		else
			printf("\n\n.globl %s\n%s:\n", name, name);
		break;
	}

	/*
	 * OP_PHI doesn't actually generate any code. It has been
	 * done by the storage allocator and the OP_PHISOURCE.
	 */
	case OP_PHI:
		break;

	case OP_PHISOURCE:
		generate_phisource(insn, state);
		break;

	/*
	 * OP_SETVAL likewise doesn't actually generate any
	 * code. On use, the "def" of the pseudo will be
	 * looked up.
	 */
	case OP_SETVAL:
		break;

	case OP_STORE:
		generate_store(insn, state);
		break;

	case OP_LOAD:
		generate_load(insn, state);
		break;

	case OP_DEATHNOTE:
		mark_pseudo_dead(state, insn->target);
		break;

	case OP_BINARY ... OP_BINARY_END:
	case OP_BINCMP ... OP_BINCMP_END:
		generate_binop(state, insn);
		break;

	case OP_BR:
		generate_branch(state, insn);
		break;

	case OP_RET:
		generate_ret(state, insn);
		break;

	default:
		output_comment(state, "unimplemented: %s", show_instruction(insn));
		break;
	}
}

static void write_reg_to_storage(struct bb_state *state, struct hardreg *reg, pseudo_t pseudo, struct storage *storage)
{
	int i;
	struct hardreg *out;

	switch (storage->type) {
	case REG_REG:
		out = hardregs + storage->regno;
		if (reg == out)
			return;
		output_insn(state, "movl %s,%s", reg->name, out->name);
		return;
	case REG_UDEF:
		if (reg->busy < 1000) {
			storage->type = REG_REG;
			storage->regno = reg - hardregs;
			reg->busy = 1000;
			return;
		}

		/* Try to find a non-busy register.. */
		for (i = 0; i < REGNO; i++) {
			out = hardregs + i;
			if (out->busy)
				continue;
			output_insn(state, "movl %s,%s", reg->name, out->name);
			storage->type = REG_REG;
			storage->regno = i;
			reg->busy = 1000;
			return;
		}

		/* Fall back on stack allocation ... */
		alloc_stack(state, storage);
		/* Fallthroigh */
	default:
		output_insn(state, "movl %s,%s", reg->name, show_memop(storage));
		return;
	}
}

static void write_val_to_storage(struct bb_state *state, pseudo_t src, struct storage *storage)
{
	struct hardreg *out;

	switch (storage->type) {
	case REG_UDEF:
		alloc_stack(state, storage);
	default:
		output_insn(state, "movl %s,%s", show_pseudo(src), show_memop(storage));
		break;
	case REG_REG:
		out = hardregs + storage->regno;
		output_insn(state, "movl %s,%s", show_pseudo(src), out->name);
	}
}

static void fill_output(struct bb_state *state, pseudo_t pseudo, struct storage *out)
{
	int i;
	struct storage_hash *in;
	struct instruction *def;

	/* Is that pseudo a constant value? */
	switch (pseudo->type) {
	case PSEUDO_VAL:
		write_val_to_storage(state, pseudo, out);
		return;
	case PSEUDO_REG:
		def = pseudo->def;
		if (def->opcode == OP_SETVAL) {
			write_val_to_storage(state, def->symbol, out);
			return;
		}
	default:
		break;
	}

	/* See if we have that pseudo in a register.. */
	for (i = 0; i < REGNO; i++) {
		struct hardreg *reg = hardregs + i;
		pseudo_t p;

		FOR_EACH_PTR(reg->contains, p) {
			if (p == pseudo) {
				write_reg_to_storage(state, reg, pseudo, out);
				return;
			}
		} END_FOR_EACH_PTR(p);
	}

	/* Do we have it in another storage? */
	in = find_storage_hash(pseudo, state->internal);
	if (!in) {
		in = find_storage_hash(pseudo, state->inputs);
		/* Undefined? */
		if (!in)
			return;
	}
	switch (out->type) {
	case REG_UDEF:
		*out = *in->storage;
		break;
	case REG_REG:
		output_insn(state, "movl %s,%s", show_memop(in->storage), hardregs[out->regno].name);
		break;
	default:
		output_insn(state, "movl %s,tmp", show_memop(in->storage));
		output_insn(state, "movl tmp,%s", show_memop(out));
		break;
	}
	return;
}

/*
 * This tries to make sure that we put all the pseudos that are
 * live on exit into the proper storage
 */
static void generate_output_storage(struct bb_state *state)
{
	struct storage_hash *entry;

	/* Go through the fixed outputs, making sure we have those regs free */
	FOR_EACH_PTR(state->outputs, entry) {
		struct storage *out = entry->storage;
		if (out->type == REG_REG) {
			struct hardreg *reg = hardregs + out->regno;
			pseudo_t p;
			int flushme = 0;

			reg->busy = 1000;
			FOR_EACH_PTR(reg->contains, p) {
				struct hardreg *dst;
				if (p == entry->pseudo) {
					flushme = -100;
					continue;
				}
				dst = preferred_reg(state, p);
				if (dst && !dst->busy) {
					DELETE_CURRENT_PTR(p);
					output_insn(state, "movl %s,%s", reg->name, dst->name);
					add_pseudo_reg(state, p, dst);
					continue;
				}
				if (CURRENT_TAG(p) & TAG_DEAD)
					continue;
				flushme++;
			} END_FOR_EACH_PTR(p);

			/* Argh. We did not contain the info we wanted, but something else instead. */
			if (flushme > 0)
				flush_reg(state, reg);
			PACK_PTR_LIST(&reg->contains);
		}
	} END_FOR_EACH_PTR(entry);

	FOR_EACH_PTR(state->outputs, entry) {
		fill_output(state, entry->pseudo, entry->storage);
	} END_FOR_EACH_PTR(entry);
}

static void generate(struct basic_block *bb, struct bb_state *state)
{
	int i;
	struct storage_hash *entry;
	struct instruction *insn;

	for (i = 0; i < REGNO; i++) {
		free_ptr_list(&hardregs[i].contains);
		hardregs[i].busy = 0;
		hardregs[i].used = 0;
	}

	FOR_EACH_PTR(state->inputs, entry) {
		struct storage *storage = entry->storage;
		const char *name = show_storage(storage);
		if (storage->type == REG_REG) {
			int regno = storage->regno;
			add_pseudo_reg(state, entry->pseudo, hardregs + regno);
			name = hardregs[regno].name;
		}
	} END_FOR_EACH_PTR(entry);

	output_label(state, ".L%p", bb);
	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		generate_one_insn(insn, state);
	} END_FOR_EACH_PTR(insn);

	if (verbose) {
		output_comment(state, "--- in ---");
		FOR_EACH_PTR(state->inputs, entry) {
			output_comment(state, "%s <- %s", show_pseudo(entry->pseudo), show_storage(entry->storage));
		} END_FOR_EACH_PTR(entry);
		output_comment(state, "--- spill ---");
		FOR_EACH_PTR(state->internal, entry) {
			output_comment(state, "%s <-> %s", show_pseudo(entry->pseudo), show_storage(entry->storage));
		} END_FOR_EACH_PTR(entry);
		output_comment(state, "--- out ---");
		FOR_EACH_PTR(state->outputs, entry) {
			output_comment(state, "%s -> %s", show_pseudo(entry->pseudo), show_storage(entry->storage));
		} END_FOR_EACH_PTR(entry);
	}
	printf("\n");
}

static void generate_list(struct basic_block_list *list, unsigned long generation)
{
	struct basic_block *bb;
	FOR_EACH_PTR(list, bb) {
		if (bb->generation == generation)
			continue;
		output_bb(bb, generation);
	} END_FOR_EACH_PTR(bb);
}

static void output_bb(struct basic_block *bb, unsigned long generation)
{
	struct bb_state state;

	bb->generation = generation;

	/* Make sure all parents have been generated first */
	generate_list(bb->parents, generation);

	state.inputs = gather_storage(bb, STOR_IN);
	state.outputs = gather_storage(bb, STOR_OUT);
	state.internal = NULL;
	state.stack_offset = 0;

	generate(bb, &state);

	free_ptr_list(&state.inputs);
	free_ptr_list(&state.outputs);

	/* Generate all children... */
	generate_list(bb->children, generation);
}

static void output(struct entrypoint *ep)
{
	unsigned long generation = ++bb_generation;

	last_reg = -1;

	/* Set up initial inter-bb storage links */
	set_up_storage(ep);

	/* Show the results ... */
	output_bb(ep->entry->bb, generation);

	/* Clear the storage hashes for the next function.. */
	free_storage();
}

static int compile(struct symbol_list *list)
{
	struct symbol *sym;
	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;
		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			output(ep);
	} END_FOR_EACH_PTR(sym);
	
	return 0;
}

int main(int argc, char **argv)
{
	return compile(sparse(argc, argv));
}

