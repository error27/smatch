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

static void generate_one_insn(struct instruction *insn,
	struct storage_hash_list *inputs,
	struct storage_hash_list *outputs)
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
	default:
		show_instruction(insn);
		break;
	}
}

static void generate(struct basic_block *bb,
	struct storage_hash_list *inputs,
	struct storage_hash_list *outputs)
{
	int i;
	struct storage_hash *entry;
	struct instruction *insn;
	struct basic_block *child;

	for (i = 0; i < REGNO; i++)
		hardregs->contains = NULL;

	FOR_EACH_PTR(inputs, entry) {
		struct storage *storage = entry->storage;
		const char *name = show_storage(storage);
		if (storage->type == REG_REG) {
			int regno = storage->regno;
			hardregs[regno].contains = entry->pseudo;
			name = hardregs[regno].name;
		}
		printf("\t%s <- %s\n", show_pseudo(entry->pseudo), name);
	} END_FOR_EACH_PTR(entry);

	FOR_EACH_PTR(bb->insns, insn) {
		if (!insn->bb)
			continue;
		generate_one_insn(insn, inputs, outputs);
	} END_FOR_EACH_PTR(insn);

	FOR_EACH_PTR(outputs, entry) {
		printf("\t%s -> %s\n", show_pseudo(entry->pseudo), show_storage(entry->storage));
	} END_FOR_EACH_PTR(entry);
	printf("\n");

	FOR_EACH_PTR(bb->children, child) {
		if (child->generation == bb->generation)
			continue;
		printf(".L%p:\n", child);
		output_bb(child, bb->generation);
	} END_FOR_EACH_PTR(child);
}

static void output_bb(struct basic_block *bb, unsigned long generation)
{
	struct storage_hash_list *inputs, *outputs;

	bb->generation = generation;
	inputs = gather_storage(bb, STOR_IN);
	outputs = gather_storage(bb, STOR_OUT);

	generate(bb, inputs, outputs);

	free_ptr_list(&inputs);
	free_ptr_list(&outputs);
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

