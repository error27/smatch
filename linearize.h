#ifndef LINEARIZE_H
#define LINEARIZE_H

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

struct basic_block_list;

struct basic_block {
	struct statement_list *stmts;
};

static inline void add_bb(struct basic_block_list **list, struct basic_block *bb)
{
	add_ptr_list((struct ptr_list **)list, bb);
}

struct entrypoint {
	struct symbol *name;
	struct symbol_list *syms;
	struct basic_block_list *bbs;
};

#endif /* LINEARIZE_H */

