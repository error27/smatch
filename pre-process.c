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
#include "parse.h"
#include "token.h"
#include "symbol.h"

#define MAXNEST (16)
static int true_nesting = 0;
static int false_nesting = 0;
static struct token *unmatched_if = NULL;
static int elif_ignore[MAXNEST];
#define if_nesting (true_nesting + false_nesting)

/*
 * This is stupid - the tokenizer already guarantees unique
 * identifiers, so we should just compare identifier pointers
 */
static int match_string_ident(struct ident *ident, const char *str)
{
	return !str[ident->len] && !memcmp(str, ident->name, ident->len);
}

static const char *show_token_sequence(struct token *token);

/* Head is one-before-list, and last is one-past-list */
static struct token *for_each_ident(struct token *head, struct token *last,
	struct token *(*action)(struct token *head, struct token *))
{
	if (!last)
		last = &eof_token_entry;
	for (;;) {
		struct token *next = head->next;

		/* Did we hit the end of the current expansion? */
		if (next == last)
			break;

		if (next->type == TOKEN_IDENT)
			next = action(head, next);

		head = next;
	}
	return head;
}

static struct token *is_defined(struct token *head, struct token *token, struct token *next)
{
	char *string[] = { "0", "1" };
	char *defined = string[lookup_symbol(token->ident, NS_PREPROCESSOR) != NULL];
	struct token *newtoken = alloc_token(token->stream, token->line, token->pos);

	newtoken->type = TOKEN_INTEGER;
	newtoken->integer = defined;
	newtoken->next = next;
	head->next = newtoken;
	return next;
}


struct token *defined_one_symbol(struct token *head, struct token *next)
{
	if (match_string_ident(next->ident, "defined")) {
		struct token *token = next->next;
		struct token *past = token->next;

		if (match_op(token, '(')) {
			token = past;
			if (!match_op(past, ')'))
				return next;
			past = past->next;
		}
		if (token->type == TOKEN_IDENT)
			return is_defined(head, token, past);
	}
	return next;
}

static struct token *expand_defined(struct token *head, struct token *last)
{
	return for_each_ident(head, last, defined_one_symbol);
}

/* Expand symbol 'sym' between 'head->next' and 'head->next->next' */
static struct token *expand(struct token *, struct symbol *);

struct token *expand_one_symbol(struct token *head, struct token *token)
{
	struct symbol *sym = lookup_symbol(token->ident, NS_PREPROCESSOR);
	if (sym && !sym->busy)
		return expand(head, sym);
	return token;
}

static struct token *expand_list(struct token *head, struct token *last)
{
	return for_each_ident(head, last, expand_one_symbol);
}

static struct token *expand(struct token *head, struct symbol *sym)
{
	struct token *expansion, *pptr, *token, *last;
	int newline;

	sym->busy++;
	token = head->next;
	newline = token->newline;

	expansion = sym->expansion;
	pptr = head;
	last = token->next;
	while (!eof_token(expansion)) {
		struct token *alloc = __alloc_token(0);

		alloc->type = expansion->type;
		alloc->stream = token->stream;
		alloc->pos = token->pos;
		alloc->newline = newline;
		alloc->line = token->line;
		alloc->next = last;
		alloc->integer = expansion->integer;
		pptr->next = alloc;
		pptr = alloc;
		expansion = expansion->next;
		newline = 0;
	}
	head = expand_list(head, last);
	sym->busy--;
	return head;
}

static const char *token_name_sequence(struct token *token, int endop, struct token *start)
{
	struct token *last;
	static char buffer[256];
	char *ptr = buffer;

	last = token;
	while (!eof_token(token) && !match_op(token, endop)) {
		int len;
		const char *val = token->string->data;
		if (token->type != TOKEN_STRING)
			val = show_token(token);
		len = strlen(val);
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
	int endlen = strlen(filename) + 1;
	const char **pptr = includepath, *path;

	while ((path = *pptr++) != NULL) {
		int fd, len = strlen(path);
		static char fullname[PATH_MAX];

		memcpy(fullname, path, len);
		memcpy(fullname+len, filename, endlen);
		fd = open(fullname, O_RDONLY);
		if (fd >= 0) {
			char * streamname = __alloc_bytes(len + endlen);
			memcpy(streamname, fullname, len + endlen);
			head->next = tokenize(streamname, fd, head->next);
			return;
		}
	}
	warn(token, "unable to open '%s'", filename);
}

static int handle_include(struct token *head, struct token *token)
{
	const char *filename;
	struct token *next;
	int expect;

	if (false_nesting)
		return 1;
	next = token->next;
	expect = '>';
	if (!match_op(next, '<')) {
		expand_list(token, NULL);
		expect = 0;
		next = token;
	}
	token = next->next;
	filename = token_name_sequence(token, expect, token);
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

static int preprocessor_if(struct token *token, int true)
{
	if (if_nesting == 0)
		unmatched_if = token;
	elif_ignore[if_nesting] = false_nesting || true;
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

static unsigned long long get_int_value(const char *str)
{
	unsigned long long value = 0;
	unsigned int base = 10, digit;

	switch (str[0]) {
	case 'x':
		base = 18;	// the -= 2 for the octal case will
		str++;		// skip the 'x'
	/* fallthrough */
	case 'o':
		str++;		// skip the 'o' or 'x/X'
		base -= 2;	// the fall-through will make this 8
	}
	while ((digit = hexval(*str)) < base) {
		value = value * base + digit;
		str++;
	}
	return value;
}

static long long primary_value(struct token *token)
{
	switch (token->type) {
	case TOKEN_INTEGER:
		return get_int_value(token->integer);
	case TOKEN_IDENT:
		warn(token, "undefined identifier in preprocessor expression");
		return 0;
	}
	error(token, "bad preprocessor expression");
	return 0;
}

static long long get_expression_value(struct expression *expr)
{
	long long left, middle, right;

	switch (expr->type) {
	case EXPR_PRIMARY:
		return primary_value(expr->token);

#define OP(x,y)	case x: return left y right;
	case EXPR_BINOP:
		left = get_expression_value(expr->left);
		if (!left && expr->op == SPECIAL_LOGICAL_AND)
			return 0;
		if (left && expr->op == SPECIAL_LOGICAL_OR)
			return 1;
		right = get_expression_value(expr->right);
		switch (expr->op) {
			OP('+',+); OP('-',-); OP('*',*); OP('/',/);
			OP('%',%); OP('<',<); OP('>',>);
			OP('&',&);OP('|',|);OP('^',^);
			OP(SPECIAL_EQUAL,==); OP(SPECIAL_NOTEQUAL,!=);
			OP(SPECIAL_LTE,<=); OP(SPECIAL_LEFTSHIFT,<<);
			OP(SPECIAL_RIGHTSHIFT,>>); OP(SPECIAL_GTE,>=);
			OP(SPECIAL_LOGICAL_AND,&&);OP(SPECIAL_LOGICAL_OR,||);
		}
		break;

#undef OP
#define OP(x,y)	case x: return y left;
	case EXPR_PREOP:
		left = get_expression_value(expr->unop);
		switch (expr->op) {
			OP('+', +); OP('-', -); OP('!', !); OP('~', ~); OP('(', );
		}
		break;

	case EXPR_CONDITIONAL:
		left = get_expression_value(expr->conditional);
		if (!expr->cond_true)
			middle = left;
		else
			middle = get_expression_value(expr->cond_true);
		right = get_expression_value(expr->cond_false);
		return left ? middle : right;
	}
	error(expr->token, "bad preprocessor expression");
	return 0;
}

extern struct token *assignment_expression(struct token *token, struct expression **tree);

static int expression_value(struct token *head)
{
	struct expression *expr;
	struct token *token;

	expand_defined(head, NULL);
	expand_list(head, NULL);
	token = assignment_expression(head->next, &expr);
	if (!eof_token(token))
		warn(token, "garbage at end: %s", show_token_sequence(token));
	return get_expression_value(expr) != 0;
}

static int handle_if(struct token *head, struct token *token)
{
	int value = 0;
	if (!false_nesting)
		value = expression_value(token);
	return preprocessor_if(token, value);
}

static int handle_elif(struct token *head, struct token *token)
{
	if (false_nesting) {
		/* If this whole if-thing is if'ed out, an elif cannot help */
		if (elif_ignore[if_nesting-1])
			return 1;
		if (expression_value(token)) {
			false_nesting--;
			true_nesting++;
			elif_ignore[if_nesting-1] = 1;
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
			if (sym) {
				head = expand(head, sym);
				continue;
			}
		}
		head = next;
	} while (!eof_token(head));
}

struct token * preprocess(struct token *token)
{
	struct token header = { 0, };

	header.next = token;
	do_preprocess(&header);
	if (if_nesting)
		warn(unmatched_if, "unmatched preprocessor conditional");

	// Drop all expressions from pre-processing, they're not used any more.
	clear_expression_alloc();

	return header.next;
}
