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

#define MAXNEST (16)
static int true_nesting = 0;
static int false_nesting = 0;
static struct token *unmatched_if = NULL;
static int elif_ignore[MAXNEST];
#define if_nesting (true_nesting + false_nesting)

static const char *show_token_sequence(struct token *token);

static struct token *expand(struct token *head, struct symbol *sym)
{
	struct token *expansion, *pptr, *token, *next;
	int newline;

	sym->busy++;
	token = head->next;
	newline = token->newline;

	expansion = sym->expansion;
	pptr = head;
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
		pptr->next = alloc;
		pptr = alloc;
		expansion = expansion->next;
		newline = 0;
	}
	for (;;) {
		struct token *next_recursive = head->next;

		/* Did we hit the end of the current expansion? */
		if (next_recursive == next)
			break;

		if (next_recursive->type == TOKEN_IDENT) {
			struct symbol *sym = lookup_symbol(next_recursive->ident, NS_PREPROCESSOR);
			if (sym && !sym->busy) {
				head = expand(head, sym);
				continue;
			}
		}
		
		head = next_recursive;
	}
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

/*
 * We could share the parsing with the "real" C parser, but
 * quite frankly expression parsing is simple enough that it's
 * just easier to not have to bother with the differences, and
 * have two separate paths instead.
 *
 * The C parser builds a parse tree for future optimization and
 * code generation, while the preprocessor parser just calculates
 * the value.  So while the parsed language is similar, the
 * differences are big.
 *
 * Types are built up from their physical sizes in bits (minus one)
 * and their "logical sizes" (ie one bit for "long", one for "short")
 * This way we can literally just or the values together and get the
 * right answer.
 */
#define PHYS_MASK	0x000ff
#define PHYS_CMASK	0x00007
#define PHYS_SMASK	0x0000f
#define PHYS_IMASK	0x0001f
#define PHYS_LMASK	0x0001f
#define PHYS_LLMASK	0x0003f

#define LOG_MASK	0x01f00
#define LOG_CMASK	0x00100
#define LOG_SMASK	0x00300
#define LOG_IMASK	0x00700
#define LOG_LMASK	0x00f00
#define LOG_LLMASK	0x01f00

#define UNSIGNEDMASK	0x10000

struct cpp_expression {
	unsigned type;		/* unsigned / long / long long / bytemasks*/
	long long value;
};

static void get_int_value(const char *str, struct cpp_expression *val)
{
	unsigned long long value = 0;
	unsigned int base = 10, digit, type;

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
	type = PHYS_IMASK | LOG_IMASK;
	val->value = value;
	while (*str) {
		if (*str == 'u' || *str == 'U')
			val->type |= UNSIGNEDMASK;
		else if (val->type & LOG_LMASK)
			val->type |= PHYS_LLMASK | LOG_LLMASK;
		else
			val->type |= PHYS_LMASK | LOG_LMASK;
		str++;
	}
}

static struct token *cpp_conditional(struct token *token, struct cpp_expression *value);
static struct token *cpp_value(struct token *token, struct cpp_expression *value)
{
	value->type = 0;
	value->value = 0;

	switch (token->type) {
	case TOKEN_INTEGER:
		get_int_value(token->integer, value);
		return token->next;
	case TOKEN_SPECIAL:
		if (token->special == '(') {
			token = cpp_conditional(token->next, value);
			token = expect(token, ')', "in preprocessor expression");
			return token;
		}
	}
	if (!eof_token(token))
		warn(token, "expected value");
	return &eof_token_entry;
}

static unsigned cpp_type(unsigned type1, unsigned type2)
{
	/* Are they of physically different sized types? */
	if ((type1 ^ type2) & PHYS_MASK) {
		/* Remove 'unsigned' from the smaller one */
		if ((type1 & PHYS_MASK) < (type2 & PHYS_MASK))
			type1 &= ~UNSIGNEDMASK;
		else
			type2 &= ~UNSIGNEDMASK;
	}
	return type1 | type2;
}			

static struct token *cpp_additive(struct token *token, struct cpp_expression *value)
{
	token = cpp_value(token, value);
	while (match_op(token, '+')) {
		struct cpp_expression righthand;
		token = cpp_value(token->next, &righthand);
		value->type = cpp_type(value->type, righthand.type);
		value->value += righthand.value;
	}
	return token;
}

static struct token *cpp_conditional(struct token *token, struct cpp_expression *value)
{
	return cpp_additive(token, value);
}
	
static int expression_value(struct token *token)
{
	struct cpp_expression expr;
	token = cpp_conditional(token, &expr);
	return expr.value != 0;
}

static int handle_if(struct token *head, struct token *token)
{
	int value = 0;
	if (!false_nesting)
		value = expression_value(token->next);
	return preprocessor_if(token, value);
}

static int handle_elif(struct token *head, struct token *token)
{
	if (false_nesting) {
		/* If this whole if-thing is if'ed out, an elif cannot help */
		if (elif_ignore[if_nesting-1])
			return 1;
		if (expression_value(token->next)) {
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
	return header.next;
}
