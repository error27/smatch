#ifndef LINEARIZE_H
#define LINEARIZE_H

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

/* Silly pseudo define. Do this right some day */
typedef struct {
	int nr;
} pseudo_t;

static inline pseudo_t to_pseudo(int nr)
{
	pseudo_t a;
	a.nr = nr;
	return a;
}

#define VOID (to_pseudo(0))

struct instruction {
	struct symbol *type;
	int opcode;
	pseudo_t target;
	union {
		pseudo_t src;	/* unops */
		struct /* binops */ {
			pseudo_t src1, src2;
		};
		struct /* multijump */ {
			int begin, end;
		};
		struct expression *val;
		struct symbol *address;
	};
};

enum opcode {
	OP_CONDTRUE,
	OP_CONDFALSE,
	OP_SETVAL,
	OP_MULTIVALUE,
	OP_MULTIJUMP,
	OP_LOAD,
	OP_STORE,
	OP_MOVE,
	OP_ARGUMENT,
	OP_CALL,
	OP_INDCALL,
	OP_UNOP = 0x200,
	OP_LASTUNOP = 0x3ff,
	OP_BINOP = 0x400,
	OP_LASTBINOP = 0x5ff,
};

struct basic_block_list;
struct instruction_list;

/*
 * Basic block flags. Right now we only have one, which keeps
 * track (at build time) whether the basic block has been branched
 * out of yet. 
 */
#define BB_HASBRANCH	0x00000001

struct basic_block {
	unsigned long flags;		/* BB status flags */
	struct symbol *this;		/* Points to the symbol that owns "this" basic block - NULL if unreachable */
	struct instruction_list *insns;	/* Linear list of instructions */
	struct symbol *next;		/* Points to the symbol that describes the fallthrough */
};

static inline void add_bb(struct basic_block_list **list, struct basic_block *bb)
{
	add_ptr_list((struct ptr_list **)list, bb);
}

static inline void add_instruction(struct instruction_list **list, struct instruction *insn)
{
	add_ptr_list((struct ptr_list **)list, insn);
}

struct entrypoint {
	struct symbol *name;
	struct symbol_list *syms;
	struct basic_block_list *bbs;
	struct basic_block *active;
};

void linearize_symbol(struct symbol *sym);

#endif /* LINEARIZE_H */

