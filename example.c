/*
 * Example of how to write a compiler with sparse
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "storage.h"

static void output_bb(struct basic_block *bb)
{
	struct storage_hash *entry;
	struct storage_hash_list *inputs, *outputs;

	inputs = gather_storage(bb, STOR_IN);
	outputs = gather_storage(bb, STOR_OUT);

	FOR_EACH_PTR(inputs, entry) {
		printf("\t%s <- %s\n", show_pseudo(entry->pseudo), show_storage(entry->storage));
	} END_FOR_EACH_PTR(entry);
	show_bb(bb);
	FOR_EACH_PTR(outputs, entry) {
		printf("\t%s -> %s\n", show_pseudo(entry->pseudo), show_storage(entry->storage));
	} END_FOR_EACH_PTR(entry);
	printf("\n");

	free_ptr_list(&inputs);
	free_ptr_list(&outputs);
}

static void output(struct entrypoint *ep)
{
	struct basic_block *bb, *prev;

	/* Set up initial inter-bb storage links */
	set_up_storage(ep);

	/* Show the results ... */
	prev = NULL;
	FOR_EACH_PTR(ep->bbs, bb) {
		output_bb(bb);
	} END_FOR_EACH_PTR(bb);

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

