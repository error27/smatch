#ifndef LIB_H
#define LIB_H
/*
 * Basic helper routine descriptions for 'sparse'.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
 */

extern unsigned int hexval(unsigned int c);

struct ident;
struct token;
struct symbol;
struct symbol_list;
struct statement;
struct statement_list;
struct expression;
struct expression_list;

struct token *skip_to(struct token *, int);
struct token *expect(struct token *, int, const char *);
extern void warn(struct token *, const char *, ...);
extern void error(struct token *, const char *, ...);

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
__DECLARE_ALLOCATOR(void, bytes);

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
extern int ptr_list_size(struct ptr_list *);

#define symbol_list_size(list) ptr_list_size((struct ptr_list *)(list))
#define statement_list_size(list) ptr_list_size((struct ptr_list *)(list))
#define expression_list_size(list) ptr_list_size((struct ptr_list *)(list))

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

#endif
