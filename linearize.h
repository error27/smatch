#ifndef LINEARIZE_H
#define LINEARIZE_H

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"

struct instruction;
struct pseudo_ptr_list;

enum pseudo_type {
	PSEUDO_VOID,
	PSEUDO_REG,
	PSEUDO_SYM,
	PSEUDO_VAL,
	PSEUDO_ARG,
	PSEUDO_PHI,
};

struct pseudo {
	int nr;
	unsigned int usage:24,
		     type:8;
	struct pseudo_ptr_list *users;
	union {
		struct symbol *sym;
		struct instruction *def;
		long long value;
	};
};

extern struct pseudo void_pseudo;

#define VOID (&void_pseudo)

struct multijmp {
	struct basic_block *target;
	int begin, end;
};

struct instruction {
	struct symbol *type;
	struct basic_block *bb;
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
			struct pseudo_list *phi_list;
		};
		struct /* unops */ {
			struct symbol *orig_type;	/* casts */
			pseudo_t src;
			unsigned int offset;		/* memops */
		};
		struct /* binops */ {
			pseudo_t src1, src2;
		};
		struct /* slice */ {
			pseudo_t base;
			unsigned from, len;
		};
		struct /* multijump */ {
			int begin, end;
		};
		struct /* setval */ {
			struct expression *val;
			struct symbol *symbol;
		};
		struct /* call */ {
			pseudo_t func;
			struct pseudo_list *arguments;
		};
		struct /* context */ {
			int increment;
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
	
	/* Logical */
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_AND_BOOL,
	OP_OR_BOOL,
	OP_BINARY_END = OP_OR_BOOL,

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

	/* Setcc - always in combination with a select or conditional branch */
	OP_SETCC,
	
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
	OP_PHISOURCE,
	OP_CAST,
	OP_CALL,
	OP_VANEXT,
	OP_VAARG,
	OP_SLICE,
	OP_SNOP,
	OP_LNOP,
	OP_NOP,

	/* Sparse tagging (line numbers, context, whatever) */
	OP_CONTEXT,
};

struct basic_block_list;
struct instruction_list;

struct basic_block {
	struct position pos;
	unsigned long generation;
	int context;
	struct basic_block_list *parents; /* sources */
	struct basic_block_list *children; /* destinations */
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

static inline void *add_pseudo(struct pseudo_list **list, struct pseudo *pseudo)
{
	return add_ptr_list((struct ptr_list **)list, pseudo);
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
	return bb != NULL;
}

static inline void add_pseudo_ptr(pseudo_t *ptr, struct pseudo_ptr_list **list)
{
	add_ptr_list((struct ptr_list **)list, ptr);
}

static inline void use_pseudo(pseudo_t p, pseudo_t *pp)
{
	*pp = p;
	if (p && p->type != PSEUDO_VOID && p->type != PSEUDO_VAL)
		add_pseudo_ptr(pp, &p->users);
}

struct entrypoint {
	struct symbol *name;
	struct symbol_list *syms;
	struct symbol_list *accesses;
	struct basic_block_list *bbs;
	struct basic_block *active;
	struct basic_block *entry;
};

extern void insert_select(struct basic_block *bb, struct instruction *br, struct instruction *phi, pseudo_t true, pseudo_t false);
extern void insert_branch(struct basic_block *bb, struct basic_block *target);

pseudo_t alloc_phi(struct basic_block *source, pseudo_t pseudo);
pseudo_t alloc_pseudo(struct instruction *def);
pseudo_t value_pseudo(long long val);

struct entrypoint *linearize_symbol(struct symbol *sym);
void show_entry(struct entrypoint *ep);

#endif /* LINEARIZE_H */

