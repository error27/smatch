#ifndef LIB_H
#define LIB_H
/*
 * Basic helper routine descriptions for 'sparse'.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *               2004 Christopher Li
 *
 *  Licensed under the Open Software License version 1.1
 */

extern unsigned int hexval(unsigned int c);

struct position {
	unsigned int type:6,
		     stream:10,
		     pos:14,
		     newline:1,
		     whitespace:1;
	unsigned int line;
};

struct ident;
struct token;
struct symbol;
struct symbol_list;
struct statement;
struct statement_list;
struct expression;
struct expression_list;
struct basic_block;
struct basic_block_list;
struct entrypoint;
struct instruction;
struct instruction_list;
struct multijmp;
struct multijmp_list;
struct phi;
struct phi_list;

struct token *skip_to(struct token *, int);
struct token *expect(struct token *, int, const char *);
extern void info(struct position, const char *, ...);
extern void warn(struct position, const char *, ...);
extern void error(struct position, const char *, ...);

#define __DECLARE_ALLOCATOR(type, x)		\
	extern type *__alloc_##x(int);		\
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


#define LIST_NODE_NR (29)

struct ptr_list {
	int nr;
	struct ptr_list *prev;
	struct ptr_list *next;
	void *list[LIST_NODE_NR];
};

struct list_iterator {
	struct ptr_list **head;
	struct ptr_list *active;
	int index;
	unsigned int flags;
};

enum iterator_br_state {
	BR_INIT,
	BR_TRUE,
	BR_FALSE,
	BR_END,
};

struct terminator_iterator {
	struct instruction *terminator;
	union {
		struct list_iterator multijmp;
		int branch;
	};
};

#define ITERATOR_BACKWARDS 1
#define ITERATOR_CURRENT 2

#define ITERATE_FIRST 1
#define ITERATE_LAST 2

#define ptr_list_empty(x) ((x) == NULL)

void iterate(struct ptr_list *,void (*callback)(void *, void *, int), void*);
void init_iterator(struct ptr_list **head, struct list_iterator *iterator, int flags);
void * next_iterator(struct list_iterator *iterator);
void delete_iterator(struct list_iterator *iterator);
void init_terminator_iterator(struct instruction* terminator, struct terminator_iterator *iterator);
struct basic_block* next_terminator_bb(struct terminator_iterator *iterator);
void replace_terminator_bb(struct terminator_iterator *iterator, struct basic_block* bb);
void * delete_ptr_list_last(struct ptr_list **head);
int replace_ptr_list(struct ptr_list **head, void *old_ptr, void *new_ptr);

extern void add_ptr_list(struct ptr_list **, void *);
extern void concat_ptr_list(struct ptr_list *a, struct ptr_list **b);
extern void free_ptr_list(struct ptr_list **);
extern int ptr_list_size(struct ptr_list *);
extern char **handle_switch(char *arg, char **next);
extern void add_pre_buffer(const char *fmt, ...);
void * next_iterator(struct list_iterator *iterator);

extern unsigned int pre_buffer_size;
extern unsigned char pre_buffer[8192];
extern int include_fd;
extern char *include;
extern int preprocess_only;

extern void create_builtin_stream(void);

static inline int symbol_list_size(struct symbol_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int statement_list_size(struct statement_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int expression_list_size(struct expression_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int instruction_list_size(struct instruction_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int phi_list_size(struct phi_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int bb_list_size(struct basic_block_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline struct basic_block* next_basic_block(struct list_iterator *iterator)
{
	return 	next_iterator(iterator);
}

static inline struct multijmp* next_multijmp(struct list_iterator *iterator)
{
	return 	next_iterator(iterator);
}

static inline void free_instruction_list(struct instruction_list **head)
{
	free_ptr_list((struct ptr_list **)head);
}

static inline void init_multijmp_iterator(struct multijmp_list **head, struct list_iterator *iterator, int flags)
{
	init_iterator((struct ptr_list **)head, iterator, flags);
}

static inline void init_bb_iterator(struct basic_block_list **head, struct list_iterator *iterator, int flags)
{
	init_iterator((struct ptr_list **)head, iterator, flags);
}

static inline struct instruction * delete_last_instruction(struct instruction_list **head)
{
	return delete_ptr_list_last((struct ptr_list **)head);
}

static inline struct basic_block * delete_last_basic_block(struct basic_block_list **head)
{
	return delete_ptr_list_last((struct ptr_list **)head);
}

static inline void *first_ptr_list(struct ptr_list *list)
{
	if (!list)
		return NULL;
	return list->list[0];
}

static inline void *last_ptr_list(struct ptr_list *list)
{

	if (!list)
		return NULL;
	list = list->prev;
	return list->list[list->nr-1];
}

static inline void * current_iterator(struct list_iterator *iterator)
{
	struct ptr_list *list = iterator->active;
	return list ? list->list[iterator->index] : NULL;
}

static inline struct basic_block *first_basic_block(struct basic_block_list *head)
{
	return last_ptr_list((struct ptr_list *)head);
}
static inline struct instruction *last_instruction(struct instruction_list *head)
{
	return last_ptr_list((struct ptr_list *)head);
}

static inline struct instruction *first_instruction(struct instruction_list *head)
{
	return first_ptr_list((struct ptr_list *)head);
}

static inline struct phi *first_phi(struct phi_list *head)
{
	return first_ptr_list((struct ptr_list *)head);
}

static inline int replace_basic_block_list(struct basic_block_list **head, struct basic_block *from, struct basic_block *to)
{
	return replace_ptr_list((struct ptr_list **)head, (void*)from, (void*)to);
}

static inline void concat_symbol_list(struct symbol_list *from, struct symbol_list **to)
{
	concat_ptr_list((struct ptr_list *)from, (struct ptr_list **)to);
}

static inline void concat_basic_block_list(struct basic_block_list *from, struct basic_block_list **to)
{
	concat_ptr_list((struct ptr_list *)from, (struct ptr_list **)to);
}

static inline void concat_instruction_list(struct instruction_list *from, struct instruction_list **to)
{
	concat_ptr_list((struct ptr_list *)from, (struct ptr_list **)to);
}

static inline void add_symbol(struct symbol_list **list, struct symbol *sym)
{
	add_ptr_list((struct ptr_list **)list, sym);
}

static inline void add_statement(struct statement_list **list, struct statement *stmt)
{
	add_ptr_list((struct ptr_list **)list, stmt);
}

static inline void add_expression(struct expression_list **list, struct expression *expr)
{
	add_ptr_list((struct ptr_list **)list, expr);
}

static inline void symbol_iterate(struct symbol_list *list, void (*callback)(struct symbol *, void *, int), void *data)
{
	iterate((struct ptr_list *)list, (void (*)(void *, void *, int))callback, data);
}

static inline void statement_iterate(struct statement_list *list, void (*callback)(struct statement *, void *, int), void *data)
{
	iterate((struct ptr_list *)list, (void (*)(void *, void *, int))callback, data);
}

static inline void expression_iterate(struct expression_list *list, void (*callback)(struct expression *, void *, int), void *data)
{
	iterate((struct ptr_list *)list, (void (*)(void *, void *, int))callback, data);
}

#define PREPARE_PTR_LIST(head, ptr)							\
	do {										\
		struct ptr_list *__head##ptr = (struct ptr_list *) (head);		\
		struct ptr_list *__list##ptr = __head##ptr;				\
		int __nr##ptr = 0;							\
		if (__head##ptr) ptr = (__typeof__(ptr)) __head##ptr->list[0];		\
		else ptr = NULL

#define NEXT_PTR_LIST(ptr)							\
		if (ptr) {								\
			if (++__nr##ptr < __list##ptr->nr) {				\
				ptr = (__typeof__(ptr)) __list##ptr->list[__nr##ptr];	\
			} else {							\
				__list##ptr = __list##ptr->next;			\
				ptr = NULL;						\
				if (__list##ptr != __head##ptr) {			\
					__nr##ptr = 0;					\
					ptr = (__typeof__(ptr)) __list##ptr->list[0];	\
				}							\
			}								\
		}

#define RESET_PTR_LIST(ptr) do {							\
		__nr##ptr = 0;								\
		__list##ptr = __head##ptr;						\
		if (__head##ptr) ptr = (__typeof__(ptr)) __head##ptr->list[0];		\
	} while (0)
	

#define FINISH_PTR_LIST(ptr)								\
		(void)(__nr##ptr); /* Sanity-check nesting */				\
	} while (0)

#define FOR_EACH_PTR(head, ptr) do {							\
	struct ptr_list *__head = (struct ptr_list *) (head);				\
	struct ptr_list *__list = __head;						\
	int __flag = ITERATE_FIRST;							\
	if (__head) {									\
		do { int __i;								\
			for (__i = 0; __i < __list->nr; __i++) {			\
				if (__i == __list->nr-1 && __list->next == __head)	\
					__flag |= ITERATE_LAST;				\
				do {							\
					ptr = (__typeof__(ptr)) (__list->list[__i]);	\
					do {

#define END_FOR_EACH_PTR		} while (0);					\
				} while (0);						\
				__flag = 0;						\
			}								\
		} while ((__list = __list->next) != __head);				\
	}										\
} while (0)

#define FOR_EACH_PTR_REVERSE(head, ptr) do {						\
	struct ptr_list *__head = (struct ptr_list *) (head);				\
	struct ptr_list *__list = __head;						\
	int __flag = ITERATE_FIRST;							\
	if (__head) {									\
		do { int __i;								\
			__list = __list->prev;						\
			__i = __list->nr;						\
			while (--__i >= 0) {						\
				if (__i == 0 && __list == __head)			\
					__flag |= ITERATE_LAST;				\
				do {							\
					ptr = (__typeof__(ptr)) (__list->list[__i]);	\
					do {

#define END_FOR_EACH_PTR_REVERSE	} while (0);					\
				} while (0);						\
				__flag = 0;						\
			}								\
		} while (__list != __head);						\
	}										\
} while (0)

#define THIS_ADDRESS(x) \
	((__typeof__(&(x))) (__list->list + __i))

#endif
