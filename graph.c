/* Copyright © International Business Machines Corp., 2006
 *
 * Author: Josh Triplett <josh@freedesktop.org>
 *
 * Licensed under the Open Software License version 1.1
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "linearize.h"

static void graph_ep(struct entrypoint *ep)
{
	struct basic_block *bb;

	printf("ep%p [label=\"%s\",shape=ellipse];\n",
	       ep, show_ident(ep->name->ident));
	FOR_EACH_PTR(ep->bbs, bb) {
		printf("bb%p [shape=record,label=\"%s:%d:%d\"]\n", bb,
		       stream_name(bb->pos.stream), bb->pos.line, bb->pos.pos);
	} END_FOR_EACH_PTR(bb);
	FOR_EACH_PTR(ep->bbs, bb) {
		struct basic_block *child;
		FOR_EACH_PTR(bb->children, child) {
			printf("bb%p -> bb%p;\n", bb, child);
		} END_FOR_EACH_PTR(child);
	} END_FOR_EACH_PTR(bb);
	printf("ep%p -> bb%p;\n", ep, ep->entry->bb);
}

static void graph_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			graph_ep(ep);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	printf("digraph control_flow {\n");
	graph_symbols(sparse_initialize(argc, argv));
	while (*argv)
		graph_symbols(sparse(argv));
	printf("}\n");
	return 0;
}
