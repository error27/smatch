#ifndef LINEARIZE_H
#define LINEARIZE_H

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

/* Silly pseudo define. Do this right some day */
struct pseudo {
	int nr;
};

typedef struct pseudo *pseudo_t;

extern struct pseudo void_pseudo;

#define VOID (&void_pseudo)

struct multijmp {
	struct basic_block *target;
	int begin, end;
};

struct phi {
	struct basic_block *source;
	pseudo_t pseudo;
};

struct instruction {
	struct symbol *type;
	int opcode;
	union {
		pseudo_t target;
		pseudo_t cond;		/* for branch and switch */
	};
	union {
		struct /* branch */ {
			struct basic_block *bb_true, *bb_false;
		};
		struct /* switch */ {
			struct multijmp_list *multijmp_list;
		};
		struct /* phi_node */ {
			struct phi_list *phi_list;
		};
		struct /* unops */ {
			struct symbol *orig_type;	/* casts */
			pseudo_t src;
		};
		struct /* binops */ {
			pseudo_t src1, src2;
		};
		struct /* multijump */ {
			int begin, end;
		};
		struct /* setval */ {
			struct expression *val;
		};
		struct /* call */ {
			pseudo_t func;
			struct pseudo_list *arguments;
		};
	};
};

enum opcode {
	OP_BADOP,
	/* Terminator */
	OP_TERMINATOR,
	OP_RET = OP_TERMINATOR,
	OP_BR,
	OP_SWITCH,
	OP_INVOKE,
	OP_COMPUTEDGOTO,
	OP_UNWIND,
	OP_TERMINATOR_END = OP_UNWIND,
	
	/* Binary */
	OP_BINARY,
	OP_ADD = OP_BINARY,
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_SHL,
	OP_SHR,
	OP_SEL,
	OP_BINARY_END = OP_SHR,
	
	/* Logical */
	OP_LOGICAL,
	OP_AND = OP_LOGICAL,
	OP_OR,
	OP_XOR,
	OP_LOGICAL_END = OP_XOR,

	/* Binary comparison */
	OP_BINCMP,
	OP_SET_EQ = OP_BINCMP,
	OP_SET_NE,
	OP_SET_LE,
	OP_SET_GE,
	OP_SET_LT,
	OP_SET_GT,
	OP_SET_B,
	OP_SET_A,
	OP_SET_BE,
	OP_SET_AE,
	OP_BINCMP_END = OP_SET_AE,

	/* Uni */
	OP_NOT,
	OP_NEG,
	
	/* Memory */
	OP_MALLOC,
	OP_FREE,
	OP_ALLOCA,
	OP_LOAD,
	OP_STORE,
	OP_SETVAL,
	OP_GET_ELEMENT_PTR,

	/* Other */
	OP_PHI,
	OP_CAST,
	OP_CALL,
	OP_VANEXT,
	OP_VAARG,
};

struct basic_block_list;
struct instruction_list;

/*
 * Basic block flags. Right now we only have one, which keeps
 * track (at build time) whether the basic block has been branched
 * out of yet. 
 */
#define BB_REACHABLE	0x00000001

struct basic_block {
	unsigned long flags;		/* BB status flags */
	struct basic_block_list *parents; /* sources */
	struct instruction_list *insns;	/* Linear list of instructions */
};

static inline int is_branch_goto(struct instruction *br)
{
	return br && br->opcode==OP_BR && (!br->bb_true || !br->bb_false);
}

static inline void add_bb(struct basic_block_list **list, struct basic_block *bb)
{
	add_ptr_list((struct ptr_list **)list, bb);
}

static inline void add_instruction(struct instruction_list **list, struct instruction *insn)
{
	add_ptr_list((struct ptr_list **)list, insn);
}

static inline void add_multijmp(struct multijmp_list **list, struct multijmp *multijmp)
{
	add_ptr_list((struct ptr_list **)list, multijmp);
}

static inline void add_phi(struct phi_list **list, struct phi *phi)
{
	add_ptr_list((struct ptr_list **)list, phi);
}

static inline void add_pseudo(struct pseudo_list **list, struct pseudo *pseudo)
{
	add_ptr_list((struct ptr_list **)list, pseudo);
}


static inline int bb_terminated(struct basic_block *bb)
{
	struct instruction *insn;
	if (!bb)
		return 0;
	insn = last_instruction(bb->insns);
	return insn && insn->opcode >= OP_RET && insn->opcode <= OP_UNWIND;
}

static inline int bb_reachable(struct basic_block *bb)
{
	return bb && (bb->parents || (bb->flags & BB_REACHABLE));
}

struct entrypoint {
	struct symbol *name;
	struct symbol_list *syms;
	struct basic_block_list *bbs;
	struct basic_block *active;
};

struct entrypoint *linearize_symbol(struct symbol *sym);
void show_entry(struct entrypoint *ep);

#endif /* LINEARIZE_H */

