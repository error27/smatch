#ifndef ALLOCATE_H
#define ALLOCATE_H

struct allocation_blob {
	struct allocation_blob *next;
	unsigned int left, offset;
	unsigned char data[];
};

struct allocator_struct {
	const char *name;
	struct allocation_blob *blobs;
	unsigned int alignment;
	unsigned int chunking;
	void *freelist;
	/* statistics */
	unsigned int allocations, total_bytes, useful_bytes;
};

extern void drop_all_allocations(struct allocator_struct *desc);
extern void *allocate(struct allocator_struct *desc, unsigned int size);
extern void free_one_entry(struct allocator_struct *desc, void *entry);

#define __DECLARE_ALLOCATOR(type, x)		\
	extern type *__alloc_##x(int);		\
	extern void __free_##x(type *);		\
	extern void show_##x##_alloc(void);	\
	extern void clear_##x##_alloc(void);
#define DECLARE_ALLOCATOR(x) __DECLARE_ALLOCATOR(struct x, x)

DECLARE_ALLOCATOR(ident);
DECLARE_ALLOCATOR(token);
DECLARE_ALLOCATOR(symbol);
DECLARE_ALLOCATOR(expression);
DECLARE_ALLOCATOR(statement);
DECLARE_ALLOCATOR(string);
DECLARE_ALLOCATOR(scope);
__DECLARE_ALLOCATOR(void, bytes);
DECLARE_ALLOCATOR(basic_block);
DECLARE_ALLOCATOR(entrypoint);
DECLARE_ALLOCATOR(instruction);
DECLARE_ALLOCATOR(multijmp);
DECLARE_ALLOCATOR(phi);
DECLARE_ALLOCATOR(pseudo);

#endif
