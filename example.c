/*
 * Example of how to write a compiler with sparse
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "flow.h"
#include "storage.h"

struct hardreg {
	const char *name;
	pseudo_t contains;
	int busy;
};

static void output_bb(struct basic_block *bb, unsigned long generation);

/*
 * We only know about the caller-clobbered registers
 * right now.
 */
static struct hardreg hardregs[] = {
	{ .name = "eax" },
	{ .name = "edx" },
	{ .name = "ecx" },
};
#define REGNO (sizeof(hardregs)/sizeof(struct hardreg))

struct bb_state {
	struct basic_block *bb;
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

/* Flush a hardreg out to the storage it has.. */
static void flush_reg(struct bb_state *state, struct hardreg *hardreg)
{
	pseudo_t pseudo;
	struct storage_hash *out;

	if (!hardreg->busy)
		return;
	pseudo = hardreg->contains;
	if (!pseudo)
		return;
	if (pseudo->type != PSEUDO_REG && pseudo->type != PSEUDO_ARG)
		return;

	out = find_storage_hash(pseudo, state->outputs);
	if (!out)
		out = find_or_create_hash(pseudo, &state->internal);
	/* FIXME! Create a private storage for this pseudo for this bb!! */
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
	default:
		return show_storage(storage);
	}
	return buffer;
}

/* Fill a hardreg with the pseudo it has */
static struct hardreg *fill_reg(struct bb_state *state, struct hardreg *hardreg, pseudo_t pseudo)
{
	struct storage_hash *src;

	switch (pseudo->type) {
	case PSEUDO_VAL:
		printf("\tmovl $%lld,%s\n", pseudo->value, hardreg->name);
		break;
	case PSEUDO_SYM:
		printf("\tmovl $<%s>,%s\n", show_pseudo(pseudo), hardreg->name);
		break;
	case PSEUDO_ARG:
	case PSEUDO_REG:
		src = find_storage_hash(pseudo, state->outputs);
		if (!src) {
			src = find_storage_hash(pseudo, state->internal);
			if (!src) {
				src = find_storage_hash(pseudo, state->inputs);
				/* Undefined? Screw it! */
				if (!src)
					break;
			}
		}
		printf("\tmov.%d %s,%s\n", 32, show_memop(src->storage), hardreg->name);
		break;
	default:
		printf("\treload %s from %s\n", hardreg->name, show_pseudo(pseudo));
		break;
	}
	hardreg->contains = pseudo;
	hardreg->busy = 1;
	return hardreg;
}

static struct hardreg *getreg(struct bb_state *state, pseudo_t pseudo)
{
	int i;
	static int last;

	for (i = 0; i < REGNO; i++) {
		if (hardregs[i].contains == pseudo) {
			last = i;
			return hardregs+i;
		}
	}
	i = last+1;
	if (i >= REGNO)
		i = 0;
	last = i;
	flush_reg(state, hardregs + i);
	return fill_reg(state, hardregs + i, pseudo);
}

static struct hardreg *prepare_address(struct bb_state *state, pseudo_t addr)
{
	if (addr->type == PSEUDO_SYM)
		return 0;
	return getreg(state, addr);
}

static const char *show_address(struct hardreg *base, struct instruction *memop)
{
	static char buffer[1000];

	if (!base) {
		struct symbol *sym = memop->src->sym;
		if (sym->ctype.modifiers & MOD_NONLOCAL) {
			sprintf(buffer, "%s+%d", show_ident(sym->ident), memop->offset);
			return buffer;
		}
		sprintf(buffer, "%d+%s(SP)", memop->offset, show_ident(sym->ident));
		return buffer;
	}
	sprintf(buffer, "%d(%s)", memop->offset, base->name);
	return buffer;
}

static void generate_store(struct instruction *insn, struct bb_state *state)
{
	struct hardreg *value = getreg(state, insn->target);
	struct hardreg *addr = prepare_address(state, insn->src);
	printf("\tmov.%d %s,%s\n", insn->size, value->name, show_address(addr, insn));
}

static void mark_pseudo_dead(pseudo_t pseudo)
{
	int i;

	for (i = 0; i < REGNO; i++) {
		if (hardregs[i].contains == pseudo)
			hardregs[i].busy = 0;
	}
}

static void generate_one_insn(struct instruction *insn, struct bb_state *state)
{
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

	case OP_DEATHNOTE:
		mark_pseudo_dead(insn->target);
		break;

	default:
		show_instruction(insn);
		break;
	}
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

static void generate(struct basic_block *bb, struct bb_state *state)
{
	int i;
	struct storage_hash *entry;
	struct instruction *insn;

	for (i = 0; i < REGNO; i++) {
		hardregs[i].contains = NULL;
		hardregs[i].busy = 0;
	}

	FOR_EACH_PTR(state->inputs, entry) {
		struct storage *storage = entry->storage;
		const char *name = show_storage(storage);
		if (storage->type == REG_REG) {
			int regno = storage->regno;
			hardregs[regno].contains = entry->pseudo;
			hardregs[regno].busy = 1;
			name = hardregs[regno].name;
		}
		printf("\t%s <- %s\n", show_pseudo(entry->pseudo), name);
	} END_FOR_EACH_PTR(entry);

	printf(".L%p:\n", bb);
	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		generate_one_insn(insn, state);
	} END_FOR_EACH_PTR(insn);

	FOR_EACH_PTR(state->outputs, entry) {
		printf("\t%s -> %s\n", show_pseudo(entry->pseudo), show_storage(entry->storage));
	} END_FOR_EACH_PTR(entry);
	printf("\n");
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

	generate(bb, &state);

	free_ptr_list(&state.inputs);
	free_ptr_list(&state.outputs);

	/* Generate all children... */
	generate_list(bb->children, generation);
}

static void output(struct entrypoint *ep)
{
	unsigned long generation = ++bb_generation;

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

