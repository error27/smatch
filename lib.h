#ifndef LIB_H
#define LIB_H
/*
 * Basic helper routine descriptions for 'sparse'.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
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
struct entrypoint;

struct token *skip_to(struct token *, int);
struct token *expect(struct token *, int, const char *);
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


#define LIST_NODE_NR (29)

struct ptr_list {
	int nr;
	struct ptr_list *prev;
	struct ptr_list *next;
	void *list[LIST_NODE_NR];
};

#define ITERATE_FIRST 1
#define ITERATE_LAST 2
void iterate(struct ptr_list *,void (*callback)(void *, void *, int), void*);
extern void add_ptr_list(struct ptr_list **, void *);
extern void concat_ptr_list(struct ptr_list *a, struct ptr_list **b);
extern void free_ptr_list(struct ptr_list **);
extern int ptr_list_size(struct ptr_list *);
extern char **handle_switch(char *arg, char **next);
extern void add_pre_buffer(const char *fmt, ...);

extern unsigned int pre_buffer_size;
extern unsigned char pre_buffer[8192];
extern int include_fd;
extern char *include;
extern int preprocess_only;

extern void create_builtin_stream(void);

#define symbol_list_size(list) ptr_list_size((struct ptr_list *)(list))
#define statement_list_size(list) ptr_list_size((struct ptr_list *)(list))
#define expression_list_size(list) ptr_list_size((struct ptr_list *)(list))

static inline void concat_symbol_list(struct symbol_list *from, struct symbol_list **to)
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
