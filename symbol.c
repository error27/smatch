#include <stdlib.h>
#include "token.h"
#include "symbol.h"

struct symbol *alloc_symbol(struct token *token, int type)
{
	struct symbol *sym = malloc(sizeof(struct symbol));
	struct ident *ident;

	if (token->type != TOKEN_IDENT)
		die("Internal error: trying to make a symbol out of a non-identifier");
	ident = token->ident;
	if (!sym)
		die("out of memory for symbol information");
	sym->token = token;
	sym->next = ident->symbol;
	sym->type = type;
	ident->symbol = sym;
	return sym;
}

struct symbol *create_symbol(int stream, const char *name, int type)
{
	return alloc_symbol(built_in_token(stream, name), type);
}

void init_symbols(void)
{
	int stream = init_stream("builtin");
	struct symbol *sym;

	sym = create_symbol(stream, "int", SYM_TYPEDEF);
}
