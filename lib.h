#ifndef LIST_H
#define LIST_H

struct ident;
struct token;
struct symbol;
struct symbol_list;
struct statement;
struct statement_list;

extern void warn(struct token *, const char *, ...);
extern void error(struct token *, const char *, ...);

#define __DECLARE_ALLOCATOR(type, x)		\
	extern type *__alloc_##x(int);		\
	extern void show_##x##_alloc(void);
#define DECLARE_ALLOCATOR(x) __DECLARE_ALLOCATOR(struct x, x)

DECLARE_ALLOCATOR(ident);
DECLARE_ALLOCATOR(token);
DECLARE_ALLOCATOR(symbol);
DECLARE_ALLOCATOR(expression);
DECLARE_ALLOCATOR(statement);
DECLARE_ALLOCATOR(string);
__DECLARE_ALLOCATOR(void, bytes);

#define LIST_NODE_NR (14)

struct ptr_list {
	int nr;
	void *list[LIST_NODE_NR];
	struct ptr_list *next;
};

void iterate(struct ptr_list *,void (*callback)(void *));
extern void add_ptr_list(struct ptr_list **, void *);

static inline void add_symbol(struct symbol_list **list, struct symbol *sym)
{
	add_ptr_list((struct ptr_list **)list, sym);
}

static inline void add_statement(struct statement_list **list, struct statement *stmt)
{
	add_ptr_list((struct ptr_list **)list, stmt);
}

static inline void symbol_iterate(struct symbol_list *list, void (*callback)(struct symbol *))
{
	iterate((struct ptr_list *)list, (void (*)(void *))callback);
}

static inline void statement_iterate(struct statement_list *list, void (*callback)(struct statement *))
{
	iterate((struct ptr_list *)list, (void (*)(void *))callback);
}

#endif
