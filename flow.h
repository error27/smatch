#ifndef FLOW_H
#define FLOW_H

extern unsigned long bb_generation;

extern void simplify_symbol_usage(struct entrypoint *ep);
extern void simplify_flow(struct entrypoint *ep);
extern void pack_basic_blocks(struct entrypoint *ep);

extern void convert_instruction_target(struct instruction *insn, pseudo_t src);
extern void cleanup_and_cse(struct entrypoint *ep);
extern int simplify_instruction(struct instruction *);

extern void kill_bb(struct basic_block *);
extern void kill_instruction(struct instruction *);

#endif
