#ifndef LIST_H
#define LIST_H

#define LIST_NODE_NR (14)

struct ptr_list {
	int nr;
	void *list[LIST_NODE_NR];
	struct ptr_list *next;
};

void iterate(struct ptr_list *,void (*callback)(void *));
extern void add_ptr_list(struct ptr_list **, void *);

struct symbol;
struct symbol_list;
struct statement;
struct statement_list;

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
