#ifndef TOKEN_H
#define TOKEN_H

/*
 * This describes the pure lexical elements (tokens), with
 * no semantic meaning. In other words, an identifier doesn't
 * have a type or meaning, it is only a specific string in
 * the input stream.
 *
 * Semantic meaning is handled elsewhere.
 */

struct stream {
	const char *name;
};

extern int input_stream_nr;
extern struct stream *input_streams;

extern int ident_hit, ident_miss;

struct ident {
	struct ident *next;	/* Hash chain of identifiers */
	struct symbol *symbols;	/* Pointer to semantic meaning list */
	unsigned char len;	/* Length of identifier name */
	char name[];		/* Actual identifier */
};

enum token_type {
	TOKEN_EOF,
	TOKEN_ERROR,
	TOKEN_IDENT,
	TOKEN_INTEGER,
	TOKEN_FP,
	TOKEN_STRING,
	TOKEN_SPECIAL,
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
	"^=",			\
}

enum special_token {
	SPECIAL_BASE = 256,
	SPECIAL_ADD_ASSIGN = 256,
	SPECIAL_INCREMENT,
	SPECIAL_MINUS_ASSIGN,
	SPECIAL_DECREMENT,
	SPECIAL_DEREFERENCE,
	SPECIAL_TIMES_ASSIGN,
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
	SPECIAL_XOR_ASSIGN
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
	unsigned int type:8,
		     stream:8,
		     pos:16;
	unsigned int line;
	struct token *next;
	union {
		unsigned long smallint;		// for "small" integers
		unsigned long long *bigint;	// for big integers
		float smallfp;			// for doubles that fit in floats (common)
		double *bigfp;			// for others
		struct ident *ident;
		unsigned int special;
		struct string *string;
	};
};

/*
 * Last token in the stream - points to itself.
 * This allows us to not test for NULL pointers
 * when following the token->next chain..
 */
extern struct token eof_token_entry;
#define eof_token(x) ((x) == &eof_token_entry)

extern int init_stream(const char *);
extern struct ident *hash_ident(struct ident *);
extern struct ident *built_in_ident(const char *);
extern struct token *built_in_token(int, const char *);
extern const char *show_special(int);
extern const char *show_ident(const struct ident *);
extern const char *show_token(const struct token *);
extern struct token * tokenize(const char *, int);
extern void die(const char *, ...);
extern void warn(struct token *, const char *, ...);
extern void show_identifier_stats(void);

#endif
