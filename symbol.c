#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "symbol.h"

const char *modifier_string(unsigned long mod)
{
	static char buffer[100];
	char *p = buffer;
	const char *res,**ptr, *names[] = {
		"auto", "register", "static", "extern",
		"char", "short", "long", "long",
		"signed", "unsigned", "const", "volatile",
		NULL
	};
	ptr = names;
	while ((res = *ptr++) != NULL) {
		if (mod & 1) {
			char c;
			*p++ = ' ';
			while ((c = *res++) != '\0')
				*p++ = c;
		}
		mod >>= 1;
	}
	*p = 0;
	return buffer+1;
}

const char *type_string(struct symbol *sym)
{
	if (sym->token)
		return sym->token->ident->name;
	if (sym == &int_type)
		return "int";
	if (sym == &fp_type)
		return "float";
	if (sym == &void_type)
		return "void";
	if (sym == &vector_type)
		return "vector";
	return "unknown";
}

void show_type(struct symbol *sym)
{
	switch (sym->type) {
	case SYM_PTR:
		printf("*(");
		show_type(sym->base_type);
		printf(")");
		break;
	case SYM_FN:
		show_type(sym->base_type);
		printf("( ... )");
		break;
	case SYM_ARRAY:
		show_type(sym->base_type);
		printf("[ ... ]");
		break;
	case SYM_TYPE:
		printf("%s %s", modifier_string(sym->modifiers), type_string(sym->base_type));
		break;
	default:
		printf("<bad type>");
	}
}

struct symbol *alloc_symbol(int type)
{
	struct symbol *sym = malloc(sizeof(struct symbol));

	memset(sym, 0, sizeof(*sym));
	if (!sym)
		die("out of memory for symbol information");
	sym->type = type;
	return sym;
}

void bind_symbol(struct symbol *sym, struct token *token)
{
	struct ident *ident;

	if (sym->token) {
		warn(token, "Symbol already bound");
		return;
	}
	if (token->type != TOKEN_IDENT)
		die("Internal error: trying to make a symbol out of a non-identifier");
	ident = token->ident;
	sym->token = token;
	sym->next = ident->symbol;
	ident->symbol = sym;
}

struct symbol *create_symbol(int stream, const char *name, int type)
{
	struct symbol *sym = alloc_symbol(type);
	bind_symbol(sym, built_in_token(stream, name));
	return sym;
}

/*
 * Type and storage class keywords need to have the symbols
 * created for them, so that the parser can have enough semantic
 * information to do parsing.
 *
 * "double" == "long float", "long double" == "long long float"
 */
struct sym_init {
	const char *name;
	struct symbol *base_type;
	unsigned int modifiers;
} symbol_init_table[] = {
	/* Storage class */
	{ "auto",	NULL,		SYM_AUTO },
	{ "register",	NULL,		SYM_REGISTER },
	{ "static",	NULL,		SYM_STATIC },
	{ "extern",	NULL,		SYM_EXTERN },

	/* Type specifiers */
	{ "void",	&void_type,	0 },
	{ "char",	&int_type,	SYM_CHAR },
	{ "short",	&int_type,	SYM_SHORT },
	{ "int",	&int_type,	0 },
	{ "long",	NULL,		SYM_LONG },
	{ "float",	&fp_type,	0 },
	{ "double",	&fp_type,	SYM_LONG },
	{ "signed",	&int_type,	SYM_SIGNED },
	{ "unsigned",	&int_type,	SYM_UNSIGNED },

	/* Type qualifiers */
	{ "const",	NULL,		SYM_CONST },
	{ "volatile",	NULL,		SYM_VOLATILE },

	{ NULL,		NULL,			0 }
};

struct symbol	void_type,
		int_type,
		fp_type,
		vector_type;

void init_symbols(void)
{
	int stream = init_stream("builtin");
	struct sym_init *ptr;

	for (ptr = symbol_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_TYPE);
		sym->base_type = ptr->base_type;
		sym->modifiers = ptr->modifiers;
	}
}
