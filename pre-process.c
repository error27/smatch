/*
 * Do C preprocessing, based on a token list gathered by
 * the tokenizer.
 *
 * This may not be the smartest preprocessor on the planet.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
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

#include "pre-process.h"
#include "lib.h"
#include "parse.h"
#include "token.h"
#include "symbol.h"
#include "expression.h"

int verbose = 0;
int preprocessing = 0;

#define MAX_NEST (256)
static int true_nesting = 0;
static int false_nesting = 0;
static struct token *unmatched_if = NULL;
static char elif_ignore[MAX_NEST];
#define if_nesting (true_nesting + false_nesting)

#define INCLUDEPATHS 32
const char *includepath[INCLUDEPATHS+1] = {
	NULL
};

const char *sys_includepath[] = {
	"/usr/include",
	"/usr/local/include",
	NULL,
};

const char *gcc_includepath[] = {
	GCC_INTERNAL_INCLUDE,
	NULL
};


/*
 * This is stupid - the tokenizer already guarantees unique
 * identifiers, so we should just compare identifier pointers
 */
int match_string_ident(struct ident *ident, const char *str)
{
	return !str[ident->len] && !memcmp(str, ident->name, ident->len);
}

static struct token *alloc_token(struct position *pos)
{
	struct token *token = __alloc_token(0);

	token->pos.stream = pos->stream;
	token->pos.line = pos->line;
	token->pos.pos = pos->pos;
	token->pos.whitespace = 1;
	return token;
}

static const char *show_token_sequence(struct token *token);

/* Head is one-before-list, and last is one-past-list */
static struct token *for_each_ident(struct token *parent, struct token *head, struct token *(*action)(struct token *parent, struct token *head, struct token *))
{
	for (;;) {
		struct token *next = head->next;

		/* Did we hit the end of the current expansion? */
		if (eof_token(next))
			break;

		if (token_type(next) == TOKEN_IDENT)
			next = action(parent, head, next);

		head = next;
	}
	return head;
}

static struct token *is_defined(struct token *head, struct token *token, struct token *next)
{
	char *string[] = { "0", "1" };
	char *defined = string[lookup_symbol(token->ident, NS_PREPROCESSOR) != NULL];
	struct token *newtoken = alloc_token(&token->pos);

	token_type(newtoken) = TOKEN_INTEGER;
	newtoken->integer = defined;
	newtoken->next = next;
	head->next = newtoken;
	return next;
}


struct token *defined_one_symbol(struct token *head, struct token *next)
{
	struct token *token = next->next;
	struct token *past = token->next;

	if (match_op(token, '(')) {
		token = past;
		past = token->next;
		if (!match_op(past, ')'))
			return next;
		past = past->next;
	}
	if (token_type(token) == TOKEN_IDENT)
		return is_defined(head, token, past);
	return next;
}

struct token variable_argument = { .next = &eof_token_entry };

/* Expand symbol 'sym' between 'head->next' and 'head->next->next' */
static struct token *expand(struct token *, struct token *, struct symbol *);

static void replace_with_string(struct token *token, const char *str)
{
	int size = strlen(str) + 1;
	struct string *s = __alloc_string(size);

	s->length = size;
	memcpy(s->data, str, size);
	token_type(token) = TOKEN_STRING;
	token->string = s;
}

static void replace_with_integer(struct token *token, unsigned int val)
{
	char *buf = __alloc_bytes(10);
	sprintf(buf, "%d", val);
	token_type(token) = TOKEN_INTEGER;
	token->integer = buf;
}

struct token *expand_one_symbol(struct token *parent, struct token *head, struct token *token)
{
	struct symbol *sym;
	struct token *x;

	/* Avoid recursive expansion */
	x = token;
	while ((x = x->parent) != NULL) {
		if (parent && x->ident == parent->ident)
			return token;
		if (x->ident == token->ident)
			return token;
	}

	sym = lookup_symbol(token->ident, NS_PREPROCESSOR);
	if (sym) {
		if (sym->arglist && !match_op(token->next, '('))
			return token;
		return expand(token, head, sym);
	}
	if (token->ident == &__LINE___ident) {
		replace_with_integer(token, token->pos.line);
	} else if (token->ident == &__FILE___ident) {
		replace_with_string(token, (input_streams + token->pos.stream)->name);
	} else if (token->ident == &defined_ident) {
		return defined_one_symbol(head, token);
	}
	return token;
}

static struct token *expand_list(struct token *parent, struct token *head)
{
	return for_each_ident(parent, head, expand_one_symbol);
}

static struct token *find_argument_end(struct token *start, struct token *arglist)
{
	int nesting = 0;

	while (!eof_token(start)) {
		struct token *next = start->next;
		if (match_op(next, '('))
			nesting++;
		else if (match_op(next, ')')) {
			if (--nesting < 0) {
				start->next = &eof_token_entry;
				return next->next;
			}
		} else if (!nesting && match_op(next, ',') && arglist->next != &variable_argument
				&& !match_op(arglist, SPECIAL_ELLIPSIS)) {
			next->special = SPECIAL_ARG_SEPARATOR;
			arglist = arglist->next;
		}
		start = next;
	}
	return start;
}

static struct token *dup_token(struct token *token, struct position *streampos, struct position *pos)
{
	struct token *alloc = alloc_token(streampos);
	token_type(alloc) = token_type(token);
	alloc->pos.newline = pos->newline;
	alloc->pos.whitespace = pos->whitespace;
	alloc->integer = token->integer;
	return alloc;	
}

static void insert(struct token *token, struct token *prev)
{
	token->next = prev->next;
	prev->next = token;
}

static struct token * replace(struct token *parent, struct token *token, struct token *prev, struct token *list)
{
	struct position *pos = &token->pos;

	prev->next = token->next;
	while (!eof_token(list) && !match_op(list, SPECIAL_ARG_SEPARATOR)) {
		struct token *newtok = dup_token(list, &token->pos, pos);
		newtok->parent = parent;
		insert(newtok, prev);
		prev = newtok;
		list = list->next;
		pos = &list->pos;
	}
	return prev;
}

static struct token *get_argument(int nr, struct token *args)
{
	if (!nr)
		return args;
	while (!eof_token(args)) {
		if (match_op(args, SPECIAL_ARG_SEPARATOR))
			if (!--nr)
				return args->next;
		args = args->next;
	}
				
	return args;
}

static struct token *stringify(struct token *token, struct token *arg)
{
	const char *s = show_token_sequence(arg);
	int size = strlen(s)+1;
	struct token *newtoken = alloc_token(&token->pos);
	struct string *string = __alloc_string(size);

	newtoken->pos.newline = token->pos.newline;
	memcpy(string->data, s, size);
	string->length = size;
	token_type(newtoken) = TOKEN_STRING;
	newtoken->string = string;
	newtoken->next = &eof_token_entry;
	return newtoken;
}

static int arg_number(struct token *arglist, struct ident *ident)
{
	int nr = 0;

	while (!eof_token(arglist)) {
		if (match_op(arglist, SPECIAL_ELLIPSIS) && ident == &__VA_ARGS___ident)
			return nr;
		if (arglist->ident == ident)
			return nr;
		nr++;
		arglist = arglist->next;
	}
	return -1;
}			

static struct token empty_arg_token = { .pos = { .type = TOKEN_EOF } };

static struct token *expand_one_arg(struct token *parent, struct token *head, struct token *token,
		struct token *arglist, struct token *arguments)
{
	int nr = arg_number(arglist, token->ident);
	struct token *orig_head = head;

	if (nr >= 0) {
		struct token *arg = get_argument(nr, arguments);
		struct token *last = token->next;
		token->next = &eof_token_entry;

		/*
		 * Special case for gcc 'x ## arg' semantics: if 'arg' is empty
		 * then the 'x' goes away too.
		 */
		if (match_op(head, SPECIAL_HASHHASH) && eof_token(arg)) {
			arg = &empty_arg_token;
			empty_arg_token.next = &eof_token_entry;
		}

		head = replace(NULL, token, head, arg);
		if (!match_op(orig_head, SPECIAL_HASHHASH) && !match_op(last, SPECIAL_HASHHASH) && !match_op(orig_head, '#'))
			head = expand_list(parent, orig_head);
		head->next = last;
		return head;
	}
	return token;
}

static void expand_arguments(struct token *parent,
	struct token *token, struct token *head,
	struct token *arguments, struct token *arglist)
{
	for (;;) {
		struct token *next = head->next;

		/* Did we hit the end of the current expansion? */
		if (eof_token(next))
			break;

		if (match_op(next, '#')) {
			struct token *nextnext = next->next;
			int nr = arg_number(arglist, nextnext->ident);
			if (nextnext != head && nr >= 0 && token_type(nextnext) == TOKEN_IDENT) {
				struct token *newtoken = stringify(nextnext, get_argument(nr, arguments));
				replace(NULL, nextnext, head, newtoken);
				continue;
			}
			warn(next->pos, "'#' operation is not followed by argument name");
		}

		if (token_type(next) == TOKEN_IDENT)
			next = expand_one_arg(parent, head, next, arglist, arguments);

		head = next;
	}
}

/*
 * Possibly valid combinations:
 *  - anything + 'empty_arg_token' is empty.
 *  - ident + ident - combine (==ident)
 *  - ident + number - combine (==ident)
 *  - number + number - combine (==number)
 *  - number + ident - combine (==number)
 *  - string + string - leave as is, C will combine them anyway
 * others cause an error and leave the two tokens as separate tokens.
 */
static struct token *hashhash(struct token *head, struct token *first, struct token *second)
{
	static char buffer[512], *p;
	struct token *newtoken;
	static const char *src;
	int len;

	first->next = second;

	/*
	 * Special case for gcc 'x ## arg' semantics: if 'arg' is empty
	 * then the 'x' goes away too.
	 *
	 * See expand_one_arg.
	 */
	if (token_type(second) == TOKEN_EOF) {
		head->next = second->next;
		return head;
	}

	p = buffer;
	switch (token_type(first)) {
	case TOKEN_INTEGER:
		len = strlen(first->integer);
		src = first->integer;
		break;
	case TOKEN_IDENT:
		len = first->ident->len;
		src = first->ident->name;
		break;
	default:
		return second;
	}
	memcpy(p, src, len);
	p += len;

	switch (token_type(second)) {
	case TOKEN_INTEGER:
		len = strlen(second->integer);
		src = second->integer;
		break;
	case TOKEN_IDENT:
		len = second->ident->len;
		src = second->ident->name;
		break;
	default:
		return second;
	}
	memcpy(p, src, len);
	p += len;
	*p++ = 0;

	newtoken = alloc_token(&first->pos);
	head->next = newtoken;
	token_type(newtoken) = token_type(first);
	switch (token_type(newtoken)) {
	case TOKEN_IDENT:
		newtoken->ident = built_in_ident(buffer);
		break;
	case TOKEN_INTEGER:
		newtoken->integer = __alloc_bytes(p - buffer);
		memcpy(newtoken->integer, buffer, p - buffer);
		break;
	}
	return newtoken;
}

static void retokenize(struct token *head)
{
	struct token * next = head->next;
	struct token * nextnext = next->next;
	struct token * nextnextnext = nextnext->next;

	if (eof_token(next) || eof_token(nextnext))
		return;

	for (;;) {
		if (eof_token(nextnextnext))
			break;
		
		if (match_op(nextnext, SPECIAL_HASHHASH)) {
			struct token *newtoken = hashhash(head, next, nextnextnext);

			next = newtoken;
			nextnext = nextnextnext->next;
			nextnextnext = nextnext->next;

			newtoken->next = nextnext;
			if (!eof_token(nextnext))
				continue;
			break;
		}

		head = next;
		next = nextnext;
		nextnext = nextnext->next;
		nextnextnext = nextnextnext->next;
	}
}

static struct token *expand(struct token *parent, struct token *head, struct symbol *sym)
{
	struct token *arguments, *token, *last;

	token = head->next;
	last = token->next;

	arguments = NULL;
	if (sym->arglist) {
		arguments = last->next;
		last = find_argument_end(last, sym->arglist);
	}
	token->next = &eof_token_entry;

	/* Replace the token with the token expansion */
	replace(parent, token, head, sym->expansion);

	/* Then, replace all the arguments with their expansions */
	if (arguments)
		expand_arguments(parent, token, head, arguments, sym->arglist);

	/* Re-tokenize the sequence if any ## token exists.. */
	retokenize(head);

	token = head;
	while (!eof_token(token->next))
		token = token->next;
	token->next = last;
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
		if (token_type(token) != TOKEN_STRING)
			val = show_token(token);
		len = strlen(val);
		memcpy(ptr, val, len);
		ptr += len;
		token = token->next;
	}
	*ptr = 0;
	if (endop && !match_op(token, endop))
		warn(start->pos, "expected '>' at end of filename");
	return buffer;
}

static int try_include(const char *path, int plen, const char *filename, int flen, struct token *head)
{
	int fd;
	static char fullname[PATH_MAX];

	memcpy(fullname, path, plen);
	if (plen && path[plen-1] != '/') {
		fullname[plen] = '/';
		plen++;
	}
	memcpy(fullname+plen, filename, flen);
	fd = open(fullname, O_RDONLY);
	if (fd >= 0) {
		char * streamname = __alloc_bytes(plen + flen);
		memcpy(streamname, fullname, plen + flen);
		head->next = tokenize(streamname, fd, head->next);
		close(fd);
		return 1;
	}
	return 0;
}

static int do_include_path(const char **pptr, struct token *head, struct token *token, const char *filename, int flen)
{
	const char *path;

	while ((path = *pptr++) != NULL) {
		if (!try_include(path, strlen(path), filename, flen, head))
			continue;
		return 1;
	}
	return 0;
}
	

static void do_include(int local, struct stream *stream, struct token *head, struct token *token, const char *filename)
{
	int flen = strlen(filename) + 1;

	/* Same directory as current stream? */
	if (local) {
		const char *path;
		char *slash;
		int plen;

		path = stream->name;
		slash = strrchr(path, '/');
		plen = slash ? slash - path : 0;

		if (try_include(path, plen, filename, flen, head))
			return;
	}

	/* Check the standard include paths.. */
	if (do_include_path(includepath, head, token, filename, flen))
		return;
	if (do_include_path(sys_includepath, head, token, filename, flen))
		return;
	if (do_include_path(gcc_includepath, head, token, filename, flen))
		return;

	error(token->pos, "unable to open '%s'", filename);
}

static int handle_include(struct stream *stream, struct token *head, struct token *token)
{
	const char *filename;
	struct token *next;
	int expect;

	if (stream->constant == -1)
		stream->constant = 0;
	if (false_nesting)
		return 1;
	next = token->next;
	expect = '>';
	if (!match_op(next, '<')) {
		expand_list(NULL, token);
		expect = 0;
		next = token;
	}
	token = next->next;
	filename = token_name_sequence(token, expect, token);
	do_include(!expect, stream, head, token, filename);
	return 1;
}

static int token_list_different(struct token *list1, struct token *list2)
{
	for (;;) {
		if (list1 == list2)
			return 0;
		if (!list1 || !list2)
			return 1;
		if (token_type(list1) != token_type(list2))
			return 1;
		list1 = list1->next;
		list2 = list2->next;
	}
}
	

static int handle_define(struct stream *stream, struct token *head, struct token *token)
{
	struct token *arglist, *expansion;
	struct token *left = token->next;
	struct symbol *sym;
	struct ident *name;

	if (token_type(left) != TOKEN_IDENT) {
		warn(head->pos, "expected identifier to 'define'");
		return 0;
	}
	if (false_nesting)
		return 1;
	name = left->ident;

	arglist = NULL;
	expansion = left->next;
	if (!expansion->pos.whitespace && match_op(expansion, '(')) {
		arglist = expansion;
		while (!eof_token(expansion)) {
			struct token *next = expansion->next;
			if (token_type(expansion) == TOKEN_IDENT) {
				if (expansion->ident == &__VA_ARGS___ident)
					warn(expansion->pos, "__VA_ARGS__ can only appear in the expansion of a C99 variadic macro");
				if (match_op(next, SPECIAL_ELLIPSIS)) {
					expansion->next = &variable_argument;
					next = next->next;
				}
			}
			if (match_op(next, ')')) {
				// Terminate the arglist
				expansion->next = &eof_token_entry;
				expansion = next->next;
				break;
			}
			if (match_op(next, ','))
				expansion->next = next->next;
			expansion = next;
		}
		arglist = arglist->next;
	}

	sym = lookup_symbol(name, NS_PREPROCESSOR);
	if (sym) {
		if (token_list_different(sym->expansion, expansion) || 
		    token_list_different(sym->arglist, arglist)) {
			warn(left->pos, "preprocessor token %.*s redefined",
					name->len, name->name);
			warn(sym->pos, "this was the original definition");
		}
		return 1;
	}
	sym = alloc_symbol(left->pos, SYM_NODE);
	bind_symbol(sym, name, NS_PREPROCESSOR);

	sym->expansion = expansion;
	sym->arglist = arglist;
	return 1;
}

static int handle_undef(struct stream *stream, struct token *head, struct token *token)
{
	struct token *left = token->next;
	struct symbol **sym;

	if (token_type(left) != TOKEN_IDENT) {
		warn(head->pos, "expected identifier to 'undef'");
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
	if (if_nesting >= MAX_NEST)
		error(token->pos, "Maximum preprocessor conditional level exhausted");
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
	if (token_type(token) == TOKEN_IDENT)
		return lookup_symbol(token->ident, NS_PREPROCESSOR) != NULL;

	warn(token->pos, "expected identifier for #if[n]def");
	return 0;
}

static int handle_ifdef(struct stream *stream, struct token *head, struct token *token)
{
	return preprocessor_if(token, token_defined(token->next));
}

static int handle_ifndef(struct stream *stream, struct token *head, struct token *token)
{
	struct token *next = token->next;
	if (stream->constant == -1) {
		int newconstant = 0;
		if (token_type(next) == TOKEN_IDENT) {
			if (!stream->protect || stream->protect == next->ident) {
				newconstant = -2;
				stream->protect = next->ident;
				stream->nesting = if_nesting+1;
			}
		}
		stream->constant = newconstant;
	}
	return preprocessor_if(token, !token_defined(next));
}

static int expression_value(struct token *head)
{
	struct expression *expr;
	struct token *token;
	long long value;

	expand_list(NULL, head);
	token = constant_expression(head->next, &expr);
	if (!eof_token(token))
		warn(token->pos, "garbage at end: %s", show_token_sequence(token));
	value = get_expression_value(expr);
	return value != 0;
}

static int handle_if(struct stream *stream, struct token *head, struct token *token)
{
	int value = 0;
	if (!false_nesting)
		value = expression_value(token);
	return preprocessor_if(token, value);
}

static int handle_elif(struct stream * stream, struct token *head, struct token *token)
{
	if (stream->nesting == if_nesting)
		stream->constant = 0;
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
	warn(token->pos, "unmatched '#elif'");
	return 1;
}

static int handle_else(struct stream *stream, struct token *head, struct token *token)
{
	if (stream->nesting == if_nesting)
		stream->constant = 0;
	if (false_nesting) {
		/* If this whole if-thing is if'ed out, an else cannot help */
		if (elif_ignore[if_nesting-1])
			return 1;
		false_nesting--;
		true_nesting++;
		elif_ignore[if_nesting-1] = 1;
		return 1;
	}
	if (true_nesting) {
		true_nesting--;
		false_nesting = 1;
		return 1;
	}
	warn(token->pos, "unmatched #else");
	return 1;
}

static int handle_endif(struct stream *stream, struct token *head, struct token *token)
{
	if (stream->constant == -2 && stream->nesting == if_nesting)
		stream->constant = -1;

	if (false_nesting) {
		false_nesting--;
		return 1;
	}
	if (true_nesting) {
		true_nesting--;
		return 1;
	}
	warn(token->pos, "unmatched #endif");
	return 1;
}

static const char *show_token_sequence(struct token *token)
{
	static char buffer[256];
	char *ptr = buffer;
	int whitespace = 0;

	if (!token)
		return "<none>";
	while (!eof_token(token) && !match_op(token, SPECIAL_ARG_SEPARATOR)) {
		const char *val = show_token(token);
		int len = strlen(val);
		if (whitespace)
			*ptr++ = ' ';
		memcpy(ptr, val, len);
		ptr += len;
		token = token->next;
		whitespace = token->pos.whitespace;
	}
	*ptr = 0;
	return buffer;
}

static int handle_warning(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	warn(token->pos, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_error(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	warn(token->pos, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_nostdinc(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	includepath[0] = NULL;
	return 1;
}

static void add_path_entry(struct token *token, const char *path)
{
	int i;

	for (i = 0; i < INCLUDEPATHS; i++) {
		if (!includepath[i]) {
			includepath[i] = path;
			includepath[i+1] = NULL;
			return;
		}
	}
	warn(token->pos, "too many include path entries");
}

static int handle_add_include(struct stream *stream, struct token *head, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token_type(token) != TOKEN_STRING) {
			warn(token->pos, "expected path string");
			return 1;
		}
		add_path_entry(token, token->string->data);
	}
}

/*
 * We replace "#pragma xxx" with "__pragma__" in the token
 * stream. Just as an example.
 *
 * We'll just #define that away for now, but the theory here
 * is that we can use this to insert arbitrary token sequences
 * to turn the pragma's into internal front-end sequences for
 * when we actually start caring about them.
 *
 * So eventually this will turn into some kind of extended
 * __attribute__() like thing, except called __pragma__(xxx).
 */
static int handle_pragma(struct stream *stream, struct token *head, struct token *token)
{
	struct token *next = head->next;

	token->ident = &pragma_ident;
	token->pos.newline = 1;
	token->pos.whitespace = 1;
	token->pos.pos = 1;
	head->next = token;
	token->next = next;
	return 1;
}

static int handle_preprocessor_command(struct stream *stream, struct token *head, struct ident *ident, struct token *token)
{
	int i;
	static struct {
		const char *name;
		int (*handler)(struct stream *, struct token *, struct token *);
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
		{ "pragma",	handle_pragma },

		// our internal preprocessor tokens
		{ "nostdinc",	handle_nostdinc },
		{ "add_include", handle_add_include },
	};

	for (i = 0; i < (sizeof (handlers) / sizeof (handlers[0])); i++) {
		if (match_string_ident(ident, handlers[i].name))
			return handlers[i].handler(stream, head, token);
	}
	return 0;
}

static void handle_preprocessor_line(struct stream *stream, struct token * head, struct token *token)
{
	if (!token)
		return;

	if (token_type(token) == TOKEN_IDENT)
		if (handle_preprocessor_command(stream, head, token->ident, token))
			return;
	warn(token->pos, "unrecognized preprocessor line '%s'", show_token_sequence(token));
}

static void preprocessor_line(struct stream *stream, struct token * head)
{
	struct token *start = head->next, *next;
	struct token **tp = &start->next;

	for (;;) {
		next = *tp;
		if (next->pos.newline)
			break;
		tp = &next->next;
	}
	head->next = next;
	*tp = &eof_token_entry;
	handle_preprocessor_line(stream, head, start->next);
}

static void do_preprocess(struct token *head)
{
	do {
		struct token *next = head->next;
		struct stream *stream = input_streams + next->pos.stream;

		if (next->pos.newline && match_op(next, '#')) {
			preprocessor_line(stream, head);
			continue;
		}

		if (false_nesting) {
			head->next = next->next;
			continue;
		}

		switch (token_type(next)) {
		case TOKEN_STREAMEND:
			if (stream->constant == -1 && stream->protect) {
				stream->constant = 1;
			}
			/* fallthrough */
		case TOKEN_STREAMBEGIN:
			head->next = next->next;
			continue;

		case TOKEN_IDENT:
			next = expand_one_symbol(next, head, next);
			/* fallthrough */
		default:
			/*
			 * Any token expansion (even if it ended up being an
			 * empty expansion) in this stream implies it can't
			 * be constant.
			 */
			stream->constant = 0;
		}

		head = next;
	} while (!eof_token(head));
}

struct token * preprocess(struct token *token)
{
	struct token header = { };

	preprocessing = 1;
	header.next = token;
	do_preprocess(&header);
	if (if_nesting)
		warn(unmatched_if->pos, "unmatched preprocessor conditional");

	// Drop all expressions from pre-processing, they're not used any more.
	clear_expression_alloc();
	preprocessing = 0;

	return header.next;
}
