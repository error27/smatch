/*
 * Do C preprocessing, based on a token list gathered by
 * the tokenizer.
 *
 * This may not be the smartest preprocessor on the planet.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include "lib.h"
#include "token.h"
#include "symbol.h"

static void expand(struct token *head, struct symbol *sym)
{
	struct token *expansion, *token, **pptr, *next;

	sym->busy++;
	token = head->next;
	fprintf(stderr, "expanding symbol '%s'\n", show_token(token));

	expansion = sym->expansion;
	pptr = &head->next;
	next = token->next;
	while (!eof_token(expansion)) {
		struct token *alloc = __alloc_token(0);

		alloc->type = expansion->type;
		alloc->stream = token->stream;
		alloc->pos = token->pos;
		alloc->newline = 0;
		alloc->line = token->line;
		alloc->next = next;
		alloc->integer = expansion->integer;
		*pptr = alloc;
		pptr = &alloc->next;
		expansion = expansion->next;
	}
	sym->busy--;
}

static int handle_preprocessor_command(struct token *head, struct ident *ident, struct token *left)
{
	if (ident->len == 6 && !memcmp(ident->name, "define", 6)) {
		struct symbol *sym;
		struct ident *name;
		if (left->type != TOKEN_IDENT)
			return 0;
		name = left->ident;
		sym = lookup_symbol(name, NS_PREPROCESSOR);
		if (sym) {
			warn(left, "preprocessor token redefined");
			return 1;
		}
		sym = alloc_symbol(left, SYM_NONE);
		bind_symbol(sym, name, NS_PREPROCESSOR);
		sym->expansion = left->next;
		return 1;
	}

	return 0;
}

static void handle_preprocessor_line(struct token * head, struct token *token)
{
	if (!token)
		return;

	if (token->type == TOKEN_IDENT)
		if (handle_preprocessor_command(head, token->ident, token->next))
			return;

	fprintf(stderr, "  +++ ");
	while (!eof_token(token)) {
		fprintf(stderr, "%s ", show_token(token));
		token = token->next;
	}
	fprintf(stderr, "+++\n");
}

static void preprocessor_line(struct token * head)
{
	struct token *start = head->next, *next;
	struct token **tp = &start->next;

	for (;;) {
		next = *tp;
		if (next->newline)
			break;
		tp = &next->next;
	}
	head->next = next;
	*tp = &eof_token_entry;
	handle_preprocessor_line(head, start->next);
}

static void do_preprocess(struct token *head)
{
	do {
		struct token *next = head->next;
		if (next->newline && match_op(next, '#')) {
			preprocessor_line(head);
			continue;
		}
		if (next->type == TOKEN_IDENT) {
			struct symbol *sym= lookup_symbol(next->ident, NS_PREPROCESSOR);
			if (sym)
				expand(head, sym);
		}
		head = next;
	} while (!eof_token(head));
}

struct token * preprocess(struct token *token)
{
	struct token header = { 0, };

	header.next = token;
	do_preprocess(&header);
	return header.next;
}
