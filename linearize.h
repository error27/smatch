#ifndef LINEARIZE_H
#define LINEARIZE_H

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

struct basic_block_list;

struct basic_block {
	struct symbol *this;		/* Points to the symbol that owns "this" basic block - NULL if unreachable */
	struct statement_list *stmts;	/* Linear list of statements */
	struct symbol *next;		/* Points to the symbol that describes the fallthrough */
};

static inline void add_bb(struct basic_block_list **list, struct basic_block *bb)
{
	add_ptr_list((struct ptr_list **)list, bb);
}

struct entrypoint {
	struct symbol *name;
	struct symbol_list *syms;
	struct basic_block_list *bbs;
	struct basic_block *active;
};

void linearize_symbol(struct symbol *sym);

#endif /* LINEARIZE_H */

