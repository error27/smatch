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
	struct symbol *symbol;	/* Pointer to semantic meaning list */
	unsigned char len;	/* Length of identifier name */
	char name[];		/* Actual identifier */
};

enum token_type {
	TOKEN_ERROR = 0,
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

struct value {
	enum token_type type;
	union {
		double fpval;
		unsigned long long intval;
		struct ident *ident;
		unsigned int special;
		struct string *string;
	};
};

struct token {
	unsigned int line;
	unsigned int pos:16,stream:8,len:8;
	struct value value;
	struct token *next;
};

extern const char *show_special(int op);
extern const char *show_token(const struct token *token);
extern struct token * tokenize(const char *, int);
extern void die(const char *, ...);
extern void warn(struct token *, const char *, ...);

#endif
