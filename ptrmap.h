#ifndef PTRMAP_H
#define PTRMAP_H

struct ptrmap;


/* ptrmap.c */
void __ptrmap_add(struct ptrmap **mapp, void *key, void *val);
void __ptrmap_update(struct ptrmap **mapp, void *key, void *val);
void *__ptrmap_lookup(struct ptrmap *map, void *key);

#endif
