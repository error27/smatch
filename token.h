#ifndef TOKEN_H
#define TOKEN_H
/*
 * Basic tokenization structures. NOTE! Those tokens had better
 * be pretty small, since we're going to keep them all in memory
 * indefinitely.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */

#include <sys/types.h>
#include "lib.h"

/*
 * This describes the pure lexical elements (tokens), with
 * no semantic meaning. In other words, an identifier doesn't
 * have a type or meaning, it is only a specific string in
 * the input stream.
 *
 * Semantic meaning is handled elsewhere.
 */

struct stream {
	int fd;
	const char *name;

	/* Use these to check for "already parsed" */
	int constant, nesting;
	struct ident *protect;
	dev_t dev;
	ino_t ino;
};

extern int input_stream_nr;
extern struct stream *input_streams;

extern int ident_hit, ident_miss;

struct ident {
	struct ident *next;	/* Hash chain of identifiers */
	struct symbol *symbols;	/* Pointer to semantic meaning list */
	unsigned char len;	/* Length of identifier name */
	unsigned char tainted;
	char name[];		/* Actual identifier */
};

enum token_type {
	TOKEN_EOF,
	TOKEN_ERROR,
	TOKEN_IDENT,
	TOKEN_NUMBER,
	TOKEN_CHAR,
	TOKEN_STRING,
	TOKEN_SPECIAL,
	TOKEN_STREAMBEGIN,
	TOKEN_STREAMEND,
	TOKEN_MACRO_ARGUMENT,
	TOKEN_STR_ARGUMENT,
	TOKEN_QUOTED_ARGUMENT,
	TOKEN_CONCAT,
	TOKEN_UNTAINT,
};

/* Combination tokens */
#define COMBINATION_STRINGS {	\
	"+=", "++",		\
	"-=", "--", "->",	\
	"*=",			\
	"/=", "/*", "//",	\
	"%=",			\
	"..", "...",		\
	"<=", "<<", "<<=",	\
	">=", ">>", ">>=",	\
	"==", "!=",		\
	"&&", "&=",		\
	"||", "|=",		\
	"^=", "##",		\
	" @ ",			\
}

enum special_token {
	SPECIAL_BASE = 256,
	SPECIAL_ADD_ASSIGN = 256,
	SPECIAL_INCREMENT,
	SPECIAL_SUB_ASSIGN,
	SPECIAL_DECREMENT,
	SPECIAL_DEREFERENCE,
	SPECIAL_MUL_ASSIGN,
	SPECIAL_DIV_ASSIGN,
	SPECIAL_COMMENT,
	SPECIAL_CPPCOMMENT,
	SPECIAL_MOD_ASSIGN,
	SPECIAL_DOTDOT,
	SPECIAL_ELLIPSIS,
	SPECIAL_LTE,
	SPECIAL_LEFTSHIFT,
	SPECIAL_SHL_ASSIGN,
	SPECIAL_GTE,
	SPECIAL_RIGHTSHIFT,
	SPECIAL_SHR_ASSIGN,
	SPECIAL_EQUAL,
	SPECIAL_NOTEQUAL,
	SPECIAL_LOGICAL_AND,
	SPECIAL_AND_ASSIGN,
	SPECIAL_LOGICAL_OR,
	SPECIAL_OR_ASSIGN,
	SPECIAL_XOR_ASSIGN,
	SPECIAL_HASHHASH,
	SPECIAL_ARG_SEPARATOR
};

struct string {
	unsigned int length;
	char data[];
};

/*
 * This is a very common data structure, it should be kept
 * as small as humanly possible. Big (rare) types go as
 * pointers.
 */
struct token {
	struct position pos;
	struct token *next;
	union {
		char *number;
		struct ident *ident;
		unsigned int special;
		struct string *string;
		int character;
		int argnum;
	};
};

#define token_type(x) ((x)->pos.type)

/*
 * Last token in the stream - points to itself.
 * This allows us to not test for NULL pointers
 * when following the token->next chain..
 */
extern int preprocessing, verbose;
extern struct token eof_token_entry;
#define eof_token(x) ((x) == &eof_token_entry)

extern int init_stream(const char *, int fd);
extern struct ident *hash_ident(struct ident *);
extern struct ident *built_in_ident(const char *);
extern struct token *built_in_token(int, const char *);
extern const char *show_special(int);
extern const char *show_ident(const struct ident *);
extern const char *show_string(const struct string *string);
extern const char *show_token(const struct token *);
extern struct token * tokenize(const char *, int, struct token *);
extern struct token * tokenize_buffer(unsigned char *, unsigned long, struct token *);

extern void die(const char *, ...);
extern void show_identifier_stats(void);
extern struct token *preprocess(struct token *);

static inline int match_op(struct token *token, int op)
{
	return token->pos.type == TOKEN_SPECIAL && token->special == op;
}

static inline int match_ident(struct token *token, struct ident *id)
{
	return token->pos.type == TOKEN_IDENT && token->ident == id;
}

#endif
