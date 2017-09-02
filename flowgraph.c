// SPDX-License-Identifier: MIT
//
// Various utilities for flowgraphs.
//
// Copyright (c) 2017 Luc Van Oostenryck.
//

#include "flowgraph.h"
#include "linearize.h"
#include "flow.h"			// for bb_generation
#include <stdio.h>


struct cfg_info {
	struct basic_block_list *list;
	unsigned long gen;
	unsigned int nr;
};


static void label_postorder(struct basic_block *bb, struct cfg_info *info)
{
	struct basic_block *child;

	if (bb->generation == info->gen)
		return;

	bb->generation = info->gen;
	FOR_EACH_PTR_REVERSE(bb->children, child) {
		label_postorder(child, info);
	} END_FOR_EACH_PTR_REVERSE(child);

	bb->postorder_nr = info->nr++;
	add_bb(&info->list, bb);
}

static void reverse_bbs(struct basic_block_list **dst, struct basic_block_list *src)
{
	struct basic_block *bb;
	FOR_EACH_PTR_REVERSE(src, bb) {
		add_bb(dst, bb);
	} END_FOR_EACH_PTR_REVERSE(bb);
}

static void debug_postorder(struct entrypoint *ep)
{
	struct basic_block *bb;

	printf("%s's reverse postorder:\n", show_ident(ep->name->ident));
	FOR_EACH_PTR(ep->bbs, bb) {
		printf("\t.L%u: %u\n", bb->nr, bb->postorder_nr);
	} END_FOR_EACH_PTR(bb);
}

//
// cfg_postorder - Set the BB's reverse postorder links
//
// Do a postorder DFS walk and set the links
// (which will do the reverse part).
//
int cfg_postorder(struct entrypoint *ep)
{
	struct cfg_info info = {
		.gen = ++bb_generation,
	};

	label_postorder(ep->entry->bb, &info);

	// OK, now info.list contains the node in postorder
	// Reuse ep->bbs for the reverse postorder.
	free_ptr_list(&ep->bbs);
	ep->bbs = NULL;
	reverse_bbs(&ep->bbs, info.list);
	free_ptr_list(&info.list);
	if (dbg_postorder)
		debug_postorder(ep);
	return info.nr;
}
