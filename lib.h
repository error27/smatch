#ifndef LIB_H
#define LIB_H

#include <stdlib.h>
#include <stddef.h>

/*
 * Basic helper routine descriptions for 'sparse'.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *               2004 Christopher Li
 *
 *  Licensed under the Open Software License version 1.1
 */

#include "compat.h"

extern int verbose, optimize, preprocessing;
extern int repeat_phase, merge_phi_sources;

#define container(ptr, type, member) \
	(type *)((void *)(ptr) - offsetof(type, member))

extern unsigned int hexval(unsigned int c);

struct position {
	unsigned int type:6,
		     stream:10,
		     newline:1,
		     whitespace:1,
		     pos:14;
	unsigned int line:31,
		     noexpand:1;
};

struct ident;
struct token;
struct symbol;
struct statement;
struct expression;
struct basic_block;
struct entrypoint;
struct instruction;
struct multijmp;
struct pseudo;

/* Silly type-safety check ;) */
#define DECLARE_PTR_LIST(listname,type)	struct listname { type *list[1]; }
#define CHECK_TYPE(head,ptr)		(void)(&(ptr) == &(head)->list[0])
#define TYPEOF(head)			__typeof__(&(head)->list[0])
#define VRFY_PTR_LIST(head)		(void)(sizeof((head)->list[0]))

DECLARE_PTR_LIST(symbol_list, struct symbol);
DECLARE_PTR_LIST(statement_list, struct statement);
DECLARE_PTR_LIST(expression_list, struct expression);
DECLARE_PTR_LIST(basic_block_list, struct basic_block);
DECLARE_PTR_LIST(instruction_list, struct instruction);
DECLARE_PTR_LIST(multijmp_list, struct multijmp);
DECLARE_PTR_LIST(pseudo_list, struct pseudo);

typedef struct pseudo *pseudo_t;

struct token *skip_to(struct token *, int);
struct token *expect(struct token *, int, const char *);
#ifdef __GNUC__
#define FORMAT_ATTR(pos) __attribute__ ((__format__ (__printf__, pos, pos+1)))
#else
#define FORMAT_ATTR(pos)
#endif
extern void die(const char *, ...) FORMAT_ATTR(1);
extern void info(struct position, const char *, ...) FORMAT_ATTR(2);
extern void warning(struct position, const char *, ...) FORMAT_ATTR(2);
extern void error(struct position, const char *, ...) FORMAT_ATTR(2);
extern void error_die(struct position, const char *, ...) FORMAT_ATTR(2);
#undef FORMAT_ATTR

#define LIST_NODE_NR (29)

struct ptr_list {
	int nr;
	struct ptr_list *prev;
	struct ptr_list *next;
	void *list[LIST_NODE_NR];
};

#define ptr_list_empty(x) ((x) == NULL)

void * delete_ptr_list_last(struct ptr_list **head);
void delete_ptr_list_entry(struct ptr_list **, void *, int);
void replace_ptr_list_entry(struct ptr_list **, void *old, void *new, int);
extern void sort_list(struct ptr_list **, int (*)(const void *, const void *));

extern void **__add_ptr_list(struct ptr_list **, void *, unsigned long tag);
extern void concat_ptr_list(struct ptr_list *a, struct ptr_list **b);
extern void __free_ptr_list(struct ptr_list **);
extern int ptr_list_size(struct ptr_list *);
extern char **handle_switch(char *arg, char **next);
extern void add_pre_buffer(const char *fmt, ...);
int linearize_ptr_list(struct ptr_list *, void **, int);

/*
 * Hey, who said that you can't do overloading in C?
 *
 * You just have to be creative, and use some gcc
 * extensions..
 */
#define add_ptr_list_tag(list,entry,tag) \
	(TYPEOF(*(list))) (CHECK_TYPE(*(list),(entry)),__add_ptr_list((struct ptr_list **)(list), (entry), (tag)))
#define add_ptr_list(list,entry) \
	add_ptr_list_tag(list,entry,0)
#define free_ptr_list(list) \
	do { VRFY_PTR_LIST(*(list)); __free_ptr_list((struct ptr_list **)(list)); } while (0)

extern unsigned int pre_buffer_size;
extern unsigned char pre_buffer[8192];
extern int include_fd;
extern char *include;
extern int preprocess_only;
extern int Wdefault_bitfield_sign;
extern int Wundefined_preprocessor;
extern int Wbitwise, Wtypesign, Wcontext;

extern void declare_builtin_functions(void);
extern void create_builtin_stream(void);
extern struct symbol_list *sparse(int argc, char **argv);

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

static inline int pseudo_list_size(struct pseudo_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int bb_list_size(struct basic_block_list* list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline void free_instruction_list(struct instruction_list **head)
{
	free_ptr_list((struct ptr_list **)head);
}

static inline struct instruction * delete_last_instruction(struct instruction_list **head)
{
	return delete_ptr_list_last((struct ptr_list **)head);
}

static inline struct basic_block * delete_last_basic_block(struct basic_block_list **head)
{
	return delete_ptr_list_last((struct ptr_list **)head);
}

#define PTR_ENTRY(h,i)	(void *)(~3UL & (unsigned long)(h)->list[i])

static inline void *first_ptr_list(struct ptr_list *list)
{
	if (!list)
		return NULL;
	return PTR_ENTRY(list, 0);
}

static inline void *last_ptr_list(struct ptr_list *list)
{

	if (!list)
		return NULL;
	list = list->prev;
	return PTR_ENTRY(list, list->nr-1);
}

static inline struct basic_block *first_basic_block(struct basic_block_list *head)
{
	return first_ptr_list((struct ptr_list *)head);
}
static inline struct instruction *last_instruction(struct instruction_list *head)
{
	return last_ptr_list((struct ptr_list *)head);
}

static inline struct instruction *first_instruction(struct instruction_list *head)
{
	return first_ptr_list((struct ptr_list *)head);
}

static inline pseudo_t first_pseudo(struct pseudo_list *head)
{
	return first_ptr_list((struct ptr_list *)head);
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
	add_ptr_list(list, sym);
}

static inline void add_statement(struct statement_list **list, struct statement *stmt)
{
	add_ptr_list(list, stmt);
}

static inline void add_expression(struct expression_list **list, struct expression *expr)
{
	add_ptr_list(list, expr);
}

#define DO_PREPARE(head, ptr, __head, __list, __nr)					\
	do {										\
		struct ptr_list *__head = (struct ptr_list *) (head);			\
		struct ptr_list *__list = __head;					\
		int __nr = 0;								\
		CHECK_TYPE(head,ptr);							\
		if (__head) ptr = PTR_ENTRY(__head, 0);					\
		else ptr = NULL

#define DO_NEXT(ptr, __head, __list, __nr)						\
		if (ptr) {								\
			if (++__nr < __list->nr) {					\
				ptr = PTR_ENTRY(__list,__nr);				\
			} else {							\
				__list = __list->next;					\
				ptr = NULL;						\
				if (__list != __head) {					\
					__nr = 0;					\
					ptr = PTR_ENTRY(__list,0);			\
				}							\
			}								\
		}

#define DO_RESET(ptr, __head, __list, __nr)						\
	do {										\
		__nr = 0;								\
		__list = __head;							\
		if (__head) ptr = PTR_ENTRY(__head, 0);					\
	} while (0)

#define DO_FINISH(ptr, __head, __list, __nr)						\
		(void)(__nr); /* Sanity-check nesting */				\
	} while (0)

#define PREPARE_PTR_LIST(head, ptr) \
	DO_PREPARE(head, ptr, __head##ptr, __list##ptr, __nr##ptr)

#define NEXT_PTR_LIST(ptr) \
	DO_NEXT(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define RESET_PTR_LIST(ptr) \
	DO_RESET(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define FINISH_PTR_LIST(ptr) \
	DO_FINISH(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define DO_FOR_EACH(head, ptr, __head, __list, __nr) do {				\
	struct ptr_list *__head = (struct ptr_list *) (head);				\
	struct ptr_list *__list = __head;						\
	CHECK_TYPE(head,ptr);								\
	if (__head) {									\
		do { int __nr;								\
			for (__nr = 0; __nr < __list->nr; __nr++) {			\
				do {							\
					ptr = PTR_ENTRY(__list,__nr);			\
					do {

#define DO_END_FOR_EACH(ptr, __head, __list, __nr)					\
					} while (0);					\
				} while (0);						\
			}								\
		} while ((__list = __list->next) != __head);				\
	}										\
} while (0)

#define DO_FOR_EACH_REVERSE(head, ptr, __head, __list, __nr) do {			\
	struct ptr_list *__head = (struct ptr_list *) (head);				\
	struct ptr_list *__list = __head;						\
	CHECK_TYPE(head,ptr);								\
	if (__head) {									\
		do { int __nr;								\
			__list = __list->prev;						\
			__nr = __list->nr;						\
			while (--__nr >= 0) {						\
				do {							\
					ptr = PTR_ENTRY(__list,__nr);			\
					do {


#define DO_END_FOR_EACH_REVERSE(ptr, __head, __list, __nr)				\
					} while (0);					\
				} while (0);						\
			}								\
		} while (__list != __head);						\
	}										\
} while (0)

#define DO_REVERSE(ptr, __head, __list, __nr, new, __newhead, __newlist, __newnr) do {	\
	struct ptr_list *__newhead = __head;						\
	struct ptr_list *__newlist = __list;						\
	int __newnr = __nr;								\
	new = ptr;									\
	goto __inside##new;								\
	if (1) {									\
		do {									\
			__newlist = __newlist->prev;					\
			__newnr = __newlist->nr;					\
	__inside##new:									\
			while (--__newnr >= 0) {					\
				do {							\
					new = PTR_ENTRY(__newlist,__newnr);		\
					do {

#define RECURSE_PTR_REVERSE(ptr, new)							\
	DO_REVERSE(ptr, __head##ptr, __list##ptr, __nr##ptr,				\
		   new, __head##new, __list##new, __nr##new)

#define DO_THIS_ADDRESS(ptr, __head, __list, __nr)					\
	((__typeof__(&(ptr))) (__list->list + __nr))

#define FOR_EACH_PTR(head, ptr) \
	DO_FOR_EACH(head, ptr, __head##ptr, __list##ptr, __nr##ptr)

#define END_FOR_EACH_PTR(ptr) \
	DO_END_FOR_EACH(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define FOR_EACH_PTR_REVERSE(head, ptr) \
	DO_FOR_EACH_REVERSE(head, ptr, __head##ptr, __list##ptr, __nr##ptr)

#define END_FOR_EACH_PTR_REVERSE(ptr) \
	DO_END_FOR_EACH_REVERSE(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define THIS_ADDRESS(ptr) \
	DO_THIS_ADDRESS(ptr, __head##ptr, __list##ptr, __nr##ptr)

extern void split_ptr_list_head(struct ptr_list *);

#define DO_SPLIT(ptr, __head, __list, __nr) do {					\
	split_ptr_list_head(__list);							\
	if (__nr >= __list->nr) {							\
		__nr -= __list->nr;							\
		__list = __list->next;							\
	};										\
} while (0)

#define DO_INSERT_CURRENT(new, ptr, __head, __list, __nr) do {				\
	void **__this, **__last;							\
	if (__list->nr == LIST_NODE_NR)							\
		DO_SPLIT(ptr, __head, __list, __nr);					\
	__this = __list->list + __nr;							\
	__last = __list->list + __list->nr - 1;						\
	while (__last >= __this) {							\
		__last[1] = __last[0];							\
		__last--;								\
	}										\
	*__this = (new);								\
	__list->nr++;									\
} while (0)

#define INSERT_CURRENT(new, ptr) \
	DO_INSERT_CURRENT(new, ptr, __head##ptr, __list##ptr, __nr##ptr)

#define DO_DELETE_CURRENT(ptr, __head, __list, __nr) do {				\
	void **__this = __list->list + __nr;						\
	void **__last = __list->list + __list->nr - 1;					\
	while (__this < __last) {							\
		__this[0] = __this[1];							\
		__this++;								\
	}										\
	*__this = (void *)0xf0f0f0f0;							\
	__list->nr--; __nr--;								\
} while (0)

#define DELETE_CURRENT_PTR(ptr) \
	DO_DELETE_CURRENT(ptr, __head##ptr, __list##ptr, __nr##ptr)

#define REPLACE_CURRENT_PTR(ptr, new_ptr)						\
	do { *THIS_ADDRESS(ptr) = (new_ptr); } while (0)

extern void pack_ptr_list(struct ptr_list **);

#define PACK_PTR_LIST(x) pack_ptr_list((struct ptr_list **)(x))

#define hashval(x) ((unsigned long)(x))

static inline void update_tag(void *p, unsigned long tag)
{
	unsigned long *ptr = p;
	*ptr = tag | (~3UL & *ptr);
}

static inline void *tag_ptr(void *ptr, unsigned long tag)
{
	return (void *)(tag | (unsigned long)ptr);
}

#define CURRENT_TAG(ptr) (3 & (unsigned long)*THIS_ADDRESS(ptr))
#define TAG_CURRENT(ptr,val)	update_tag(THIS_ADDRESS(ptr),val)

#endif
