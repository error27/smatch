#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "token.h"

/*
 * An identifier with semantic meaning is a "symbol".
 *
 * There's a 1:n relationship: each symbol is always
 * associated with one identifier, while each identifier
 * can have one or more semantic meanings due to C scope
 * rules.
 *
 * The progression is symbol -> token -> identifier. The
 * token contains the information on where the symbol was
 * declared.
 */
struct symbol {
	struct token *token;		/* Where this symbol was declared */
	struct symbol *next;		/* Next semantic symbol that shares this identifier */
	int type;
};

enum symbol_types {
	SYM_NONE = 0,			/* regular variable */
	SYM_MEMBER,			/* structure member */
	SYM_TYPEDEF,			/* typedef */
	SYM_SPECIFIER,			/* specifier */
	SYM_QUALIFIER,			/* type qualifier */
};

#define symbol_is_typename(sym) ((sym)->type >= SYM_TYPEDEF)

void init_symbols(void);

#endif /* SEMANTIC_H */

