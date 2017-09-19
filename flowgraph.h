#ifndef FLOWGRAPH_H
#define FLOWGRAPH_H

struct entrypoint;

int cfg_postorder(struct entrypoint *ep);
void domtree_build(struct entrypoint *ep);

#endif
