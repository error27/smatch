#ifndef FLOW_H
#define FLOW_H

extern void simplify_symbol_usage(struct entrypoint *ep);
extern void simplify_phi_nodes(struct entrypoint *ep);
extern void pack_basic_blocks(struct entrypoint *ep);

#endif
