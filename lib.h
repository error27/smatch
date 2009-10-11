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
#include "ptrlist.h"

#define DO_STRINGIFY(x) #x
#define STRINGIFY(x) DO_STRINGIFY(x)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#endif

extern int verbose, optimize, optimize_size, preprocessing;
extern int die_if_error;
extern int repeat_phase, merge_phi_sources;
extern int gcc_major, gcc_minor, gcc_patchlevel;

extern unsigned int hexval(unsigned int c);

struct position {
	unsigned int type:6,
		     stream:14,
		     newline:1,
		     whitespace:1,
		     pos:10;
	unsigned int line:31,
		     noexpand:1;
};

struct cmdline_include {
	char *filename;
	int fd;
};

extern struct cmdline_include cmdline_include[];
extern int cmdline_include_nr;


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

DECLARE_PTR_LIST(symbol_list, struct symbol);
DECLARE_PTR_LIST(statement_list, struct statement);
DECLARE_PTR_LIST(expression_list, struct expression);
DECLARE_PTR_LIST(basic_block_list, struct basic_block);
DECLARE_PTR_LIST(instruction_list, struct instruction);
DECLARE_PTR_LIST(multijmp_list, struct multijmp);
DECLARE_PTR_LIST(pseudo_list, struct pseudo);
DECLARE_PTR_LIST(string_list, char);

typedef struct pseudo *pseudo_t;

struct token *skip_to(struct token *, int);
struct token *expect(struct token *, int, const char *);
#ifdef __GNUC__
#define FORMAT_ATTR(pos) __attribute__ ((__format__ (__printf__, pos, pos+1)))
#define NORETURN_ATTR __attribute__ ((__noreturn__))
#define SENTINEL_ATTR __attribute__ ((__sentinel__))
#else
#define FORMAT_ATTR(pos)
#define NORETURN_ATTR
#define SENTINEL_ATTR
#endif
extern void die(const char *, ...) FORMAT_ATTR(1) NORETURN_ATTR;
extern void info(struct position, const char *, ...) FORMAT_ATTR(2);
extern void warning(struct position, const char *, ...) FORMAT_ATTR(2);
extern void sparse_error(struct position, const char *, ...) FORMAT_ATTR(2);
extern void error_die(struct position, const char *, ...) FORMAT_ATTR(2) NORETURN_ATTR;
extern void expression_error(struct expression *, const char *, ...) FORMAT_ATTR(2);

extern void add_pre_buffer(const char *fmt, ...) FORMAT_ATTR(1);

extern int preprocess_only;

extern int Waddress_space;
extern int Wbitwise;
extern int Wcast_to_as;
extern int Wcast_truncate;
extern int Wcontext;
extern int Wdecl;
extern int Wdefault_bitfield_sign;
extern int Wdesignated_init;
extern int Wdo_while;
extern int Wenum_mismatch;
extern int Wnon_pointer_null;
extern int Wold_initializer;
extern int Wone_bit_signed_bitfield;
extern int Wparen_string;
extern int Wptr_subtraction_blows;
extern int Wreturn_void;
extern int Wshadow;
extern int Wtransparent_union;
extern int Wtypesign;
extern int Wundef;
extern int Wuninitialized;
extern int Wdeclarationafterstatement;

extern int dbg_entry;
extern int dbg_dead;

extern void declare_builtin_functions(void);
extern void create_builtin_stream(void);
extern struct symbol_list *sparse_initialize(int argc, char **argv, struct string_list **files);
extern struct symbol_list *__sparse(char *filename);
extern struct symbol_list *sparse_keep_tokens(char *filename);
extern struct symbol_list *sparse(char *filename);

static inline int symbol_list_size(struct symbol_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int statement_list_size(struct statement_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int expression_list_size(struct expression_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int instruction_list_size(struct instruction_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int pseudo_list_size(struct pseudo_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline int bb_list_size(struct basic_block_list *list)
{
	return ptr_list_size((struct ptr_list *)(list));
}

static inline void free_instruction_list(struct instruction_list **head)
{
	free_ptr_list((struct ptr_list **)head);
}

static inline struct instruction * delete_last_instruction(struct instruction_list **head)
{
	return undo_ptr_list_last((struct ptr_list **)head);
}

static inline struct basic_block * delete_last_basic_block(struct basic_block_list **head)
{
	return delete_ptr_list_last((struct ptr_list **)head);
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

#define hashval(x) ((unsigned long)(x))

#endif
