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
#include <fcntl.h>
#include <limits.h>

#include "lib.h"
#include "token.h"
#include "symbol.h"

static int true_nesting = 0;
static int false_nesting = 0;
static struct token *unmatched_if = NULL;
#define if_nesting (true_nesting + false_nesting)

static void expand(struct token *head, struct symbol *sym)
{
	struct token *expansion, *token, **pptr, *next;
	int newline;

	sym->busy++;
	token = head->next;
	newline = token->newline;
	fprintf(stderr, "expanding symbol '%s'\n", show_token(token));

	expansion = sym->expansion;
	pptr = &head->next;
	next = token->next;
	while (!eof_token(expansion)) {
		struct token *alloc = __alloc_token(0);

		alloc->type = expansion->type;
		alloc->stream = token->stream;
		alloc->pos = token->pos;
		alloc->newline = newline;
		alloc->line = token->line;
		alloc->next = next;
		alloc->integer = expansion->integer;
		*pptr = alloc;
		pptr = &alloc->next;
		expansion = expansion->next;
		newline = 0;
	}
	sym->busy--;
}

static const char *token_name_sequence(struct token *token, int endop, struct token *start)
{
	struct token *last;
	static char buffer[256];
	char *ptr = buffer;

	last = token;
	while (!eof_token(token) && !match_op(token, endop)) {
		const char *val = show_token(token);
		int len = strlen(val);
		memcpy(ptr, val, len);
		ptr += len;
		token = token->next;
	}
	*ptr = 0;
	if (endop && !match_op(token, endop))
		warn(start, "expected '>' at end of filename");
	return buffer;
}

static const char *includepath[] = {
	"/usr/include/",
	"/usr/local/include/",
	"",
	NULL
};

static void do_include(struct token *head, struct token *token, const char *filename)
{
	const char **pptr = includepath, *path;

	while ((path = *pptr++) != NULL) {
		int fd, len = strlen(path);
		static char fullname[PATH_MAX];

		memcpy(fullname, path, len);
		strcpy(fullname+len, filename);
		fd = open(fullname, O_RDONLY);
		if (fd >= 0) {
			head->next = tokenize(filename, fd, head->next);
			return;
		}
	}
	warn(token, "unable to open '%s'", filename);
}

static int handle_include(struct token *head, struct token *token)
{
	const char *filename;

	if (false_nesting)
		return 1;

	token = token->next;
	if (token->type == TOKEN_STRING)
		filename = token->string->data;
	else if (match_op(token, '<'))
		filename = token_name_sequence(token->next, '>', token);
	else
		filename = token_name_sequence(token, 0, token);
	do_include(head, token, filename);
	return 1;
}

static int handle_define(struct token *head, struct token *token)
{
	struct token *left = token->next;
	struct symbol *sym;
	struct ident *name;

	if (left->type != TOKEN_IDENT) {
		warn(head, "expected identifier to 'define'");
		return 0;
	}
	if (false_nesting)
		return 1;
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

static int handle_undef(struct token *head, struct token *token)
{
	struct token *left = token->next;
	struct symbol **sym;

	if (left->type != TOKEN_IDENT) {
		warn(head, "expected identifier to 'undef'");
		return 0;
	}
	if (false_nesting)
		return 1;
	sym = &left->ident->symbols;
	while (*sym) {
		struct symbol *t = *sym;
		if (t->namespace == NS_PREPROCESSOR) {
			*sym = t->next_id;
			return 1;
		}
		sym = &t->next_id;
	}
	return 1;
}

/*
 * This is stupid - the tokenizer already guarantees unique
 * identifiers, so we should just compare identifier pointers
 */
static int match_string_ident(struct ident *ident, const char *str)
{
	return !str[ident->len] && !memcmp(str, ident->name, ident->len);
}

static int preprocessor_if(struct token *token, int true)
{
	if (if_nesting == 0)
		unmatched_if = token;
	if (false_nesting || !true) {
		false_nesting++;
		return 1;
	}
	true_nesting++;
	return 1;
}

static int token_defined(struct token *token)
{
	if (token->type == TOKEN_IDENT)
		return lookup_symbol(token->ident, NS_PREPROCESSOR) != NULL;

	warn(token, "expected identifier for #if[n]def");
	return 0;
}

static int handle_ifdef(struct token *head, struct token *token)
{
	return preprocessor_if(token, token_defined(token->next));
}

static int handle_ifndef(struct token *head, struct token *token)
{
	return preprocessor_if(token, !token_defined(token->next));
}

static int expression_value(struct token *token)
{
	return 0;
}

static int handle_if(struct token *head, struct token *token)
{
	if (false_nesting) {
		false_nesting++;
		return 1;
	}
	return preprocessor_if(token, expression_value(token->next));
}

static int handle_elif(struct token *head, struct token *token)
{
	/* if we're deep inside a false, 'elif' is a no-op */
	if (false_nesting > 1)
		return 1;
	if (false_nesting == 1) {
		if (expression_value(token->next)) {
			false_nesting--;
			true_nesting++;
		}
		return 1;
	}
	if (true_nesting) {
		false_nesting = 1;
		true_nesting--;
		return 1;
	}
	warn(token, "unmatched '#elif'");
	return 1;
}

static int handle_else(struct token *head, struct token *token)
{
	// else inside a _nesting_ false is a no-op
	if (false_nesting > 1)
		return 1;
	// but if we have just one false, this else
	if (false_nesting) {
		true_nesting += false_nesting;
		false_nesting = 0;
		return 1;
	}
	if (true_nesting) {
		true_nesting--;
		false_nesting = 1;
		return 1;
	}
	warn(token, "unmatched #else");
	return 1;
}

static int handle_endif(struct token *head, struct token *token)
{
	if (false_nesting) {
		false_nesting--;
		return 1;
	}
	if (true_nesting) {
		true_nesting--;
		return 1;
	}
	warn(token, "unmatched #endif");
	return 1;
}

static const char *show_token_sequence(struct token *token)
{
	static char buffer[256];
	char *ptr = buffer;

	while (!eof_token(token)) {
		const char *val = show_token(token);
		int len = strlen(val);
		*ptr++ = ' ';
		memcpy(ptr, val, len);
		ptr += len;
		token = token->next;
	}
	*ptr++ = 0;
	*ptr = 0;
	return buffer+1;
}

static int handle_warning(struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	warn(token, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_error(struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	error(token, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_preprocessor_command(struct token *head, struct ident *ident, struct token *token)
{
	int i;
	static struct {
		const char *name;
		int (*handler)(struct token *, struct token *);
	} handlers[] = {
		{ "define",	handle_define },
		{ "undef",	handle_undef },
		{ "ifdef",	handle_ifdef },
		{ "ifndef",	handle_ifndef },
		{ "else",	handle_else },
		{ "endif",	handle_endif },
		{ "if",		handle_if },
		{ "elif",	handle_elif },
		{ "warning",	handle_warning },
		{ "error",	handle_error },
		{ "include",	handle_include },
	};

	for (i = 0; i < (sizeof (handlers) / sizeof (handlers[0])); i++) {
		if (match_string_ident(ident, handlers[i].name))
			return handlers[i].handler(head, token);
	}
	return 0;
}

static void handle_preprocessor_line(struct token * head, struct token *token)
{
	if (!token)
		return;

	if (token->type == TOKEN_IDENT)
		if (handle_preprocessor_command(head, token->ident, token))
			return;
	warn(token, "unrecognized preprocessor line '%s'", show_token_sequence(token));
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
		if (false_nesting) {
			head->next = next->next;
			continue;
		}
		if (next->type == TOKEN_IDENT) {
			struct symbol *sym= lookup_symbol(next->ident, NS_PREPROCESSOR);
			if (sym)
				expand(head, sym);
				
		}
		head = head->next;
	} while (!eof_token(head));
}

struct token * preprocess(struct token *token)
{
	struct token header = { 0, };

	header.next = token;
	do_preprocess(&header);
	if (if_nesting)
		warn(unmatched_if, "unmatched preprocessor conditional");
	return header.next;
}
