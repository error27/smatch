#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "token.h"
#include "symbol.h"

struct symbol *lookup_symbol(struct ident *ident, enum namespace ns)
{
	struct symbol *sym;

	for (sym = ident->symbols; sym; sym = sym->next_id) {
		if (sym->namespace == ns)
			return sym;
	}
	return sym;
}

const char *modifier_string(unsigned long mod)
{
	static char buffer[100];
	char *p = buffer;
	const char *res,**ptr, *names[] = {
		"auto", "register", "static", "extern",
		"const", "volatile", "signed", "unsigned",
		"char", "short", "long", "long",
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
	*p++ = 0;
	*p++ = 0;
	return buffer+1;
}

const char *type_string(unsigned int modifiers, struct symbol *sym)
{
	if (!sym)
		return "<notype>";
		
	if (sym->token)
		return show_token(sym->token);
	if (sym == &int_type) {
		if (modifiers & (SYM_CHAR | SYM_SHORT | SYM_LONG))
			return "";
		return "int";
	}
	if (sym == &fp_type)
		return "float";
	if (sym == &void_type)
		return "void";
	if (sym == &vector_type)
		return "vector";
	if (sym == &bad_type)
		return "bad type";
	return "typedef";
}

static void show_one_symbol(struct symbol *sym)
{
	printf("Symbol %s:\n\t", show_token(sym->token));
	show_type(sym);
	printf("\n");
}

void show_symbol_list(struct symbol_list *list)
{
	symbol_iterate(list, show_one_symbol);
}

void show_type_list(struct symbol *sym)
{
	while (sym) {
		show_one_symbol(sym);
		sym = sym->next;
	}
}

void show_type(struct symbol *sym)
{
	if (!sym) {
		printf("<nosym>");
		return;
	}

	switch (sym->type) {
	case SYM_PTR:
		printf("%s", modifier_string(sym->modifiers));
		printf("*(");
		show_type(sym->base_type);
		printf(")");
		break;

	case SYM_FN:
		printf("%s", modifier_string(sym->modifiers));
		show_type(sym->base_type);

		printf("(\n");
		show_type_list(sym->children);
		printf(" )");
		break;

	case SYM_ARRAY:
		printf("%s", modifier_string(sym->modifiers));
		show_type(sym->base_type);
		printf("[ ... ]");
		break;

	case SYM_TYPE:
		printf("%s %s", modifier_string(sym->modifiers), type_string(sym->modifiers, sym->base_type));
		break;

	default:
		printf("<bad type>");
	}
}

void show_symbol(struct symbol *sym)
{
	printf("Symbol %s:\n  ", show_token(sym->token));
	show_type(sym);
	printf("\n");
}

struct symbol *alloc_symbol(struct token *token, int type)
{
	struct symbol *sym = malloc(sizeof(struct symbol));

	if (!sym)
		die("out of memory for symbol information");
	memset(sym, 0, sizeof(*sym));
	sym->type = type;
	sym->token = token;
	return sym;
}

void bind_symbol(struct symbol *sym, struct ident *ident, enum namespace ns)
{
	sym->namespace = ns;
	sym->next_id = ident->symbols;
	ident->symbols = sym;
}

struct symbol *create_symbol(int stream, const char *name, int type)
{
	struct token *token = built_in_token(stream, name);
	struct symbol *sym = alloc_symbol(token, type);
	bind_symbol(sym, token->ident, NS_TYPEDEF);
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
	{ "__const",	NULL,		SYM_CONST },
	{ "volatile",	NULL,		SYM_VOLATILE },

	/* Typedef.. */
	{ "typedef",	NULL,		SYM_TYPEDEF },

	/* Ignored for now.. */
	{ "inline",	NULL,		0 },
	{ "__inline",	NULL,		0 },
	{ "__inline__",	NULL,		0 },
	{ "restrict",	NULL,		0 },
	{ "__restrict",	NULL,		0 },

	{ NULL,		NULL,			0 }
};

struct symbol	void_type,
		int_type,
		fp_type,
		vector_type,
		bad_type;

#define IDENT(n) \
	struct ident n ## _ident = { len: sizeof(#n)-1, name: #n }

IDENT(struct); IDENT(union); IDENT(enum);
IDENT(sizeof);

void init_symbols(void)
{
	int stream = init_stream("builtin");
	struct sym_init *ptr;

	hash_ident(&struct_ident);
	hash_ident(&union_ident);
	hash_ident(&enum_ident);
	hash_ident(&sizeof_ident);
	for (ptr = symbol_init_table; ptr->name; ptr++) {
		struct symbol *sym;
		sym = create_symbol(stream, ptr->name, SYM_TYPE);
		sym->base_type = ptr->base_type;
		sym->modifiers = ptr->modifiers;
	}
}
