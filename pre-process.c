/*
 * Do C preprocessing, based on a token list gathered by
 * the tokenizer.
 *
 * This may not be the smartest preprocessor on the planet.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
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
#include "expression.h"

#define MAXNEST (16)
static int true_nesting = 0;
static int false_nesting = 0;
static struct token *unmatched_if = NULL;
static int elif_ignore[MAXNEST];
#define if_nesting (true_nesting + false_nesting)

#define INCLUDEPATHS 32
const char *includepath[INCLUDEPATHS+1] = {
	"/usr/lib/gcc-lib/i386-redhat-linux/3.2.1/include/",
	"/usr/include/",
	"/usr/local/include/",
	"",
	NULL
};



/*
 * This is stupid - the tokenizer already guarantees unique
 * identifiers, so we should just compare identifier pointers
 */
static int match_string_ident(struct ident *ident, const char *str)
{
	return !str[ident->len] && !memcmp(str, ident->name, ident->len);
}

static struct token *alloc_token(struct token *dup)
{
	struct token *token = __alloc_token(0);

	token->stream = dup->stream;
	token->line = dup->line;
	token->pos = dup->pos;
	token->whitespace = 1;
	return token;
}

static const char *show_token_sequence(struct token *token);

/* Head is one-before-list, and last is one-past-list */
static struct token *for_each_ident(struct token *head, struct token *(*action)(struct token *head, struct token *))
{
	for (;;) {
		struct token *next = head->next;

		/* Did we hit the end of the current expansion? */
		if (eof_token(next))
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
	struct token *newtoken = alloc_token(token);

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
			past = token->next;
			if (!match_op(past, ')'))
				return next;
			past = past->next;
		}
		if (token->type == TOKEN_IDENT)
			return is_defined(head, token, past);
	}
	return next;
}

static struct token *expand_defined(struct token *head)
{
	return for_each_ident(head, defined_one_symbol);
}

/* Expand symbol 'sym' between 'head->next' and 'head->next->next' */
static struct token *expand(struct token *, struct symbol *);

static void replace_with_string(struct token *token, const char *str)
{
	int size = strlen(str) + 1;
	struct string *s = __alloc_string(size);

	s->length = size;
	memcpy(s->data, str, size);
	token->type = TOKEN_STRING;
	token->string = s;
}

static void replace_with_integer(struct token *token, unsigned int val)
{
	char *buf = __alloc_bytes(10);
	sprintf(buf, "%d", val);
	token->type = TOKEN_INTEGER;
	token->integer = buf;
}

struct token *expand_one_symbol(struct token *head, struct token *token)
{
	struct symbol *sym = lookup_symbol(token->ident, NS_PREPROCESSOR);
	if (sym && !sym->busy) {
		if (sym->arglist && !match_op(token->next, '('))
			return token;
		return expand(head, sym);
	}
	if (!memcmp(token->ident->name, "__LINE__", 9)) {
		replace_with_integer(token, token->line);
	} else if (!memcmp(token->ident->name, "__FILE__", 9)) {
		replace_with_string(token, (input_streams + token->stream)->name);
	}
	return token;
}

static struct token *expand_list(struct token *head)
{
	return for_each_ident(head, expand_one_symbol);
}

static struct token *find_argument_end(struct token *start)
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
		} else if (!nesting && match_op(next, ','))
			next->special = SPECIAL_ARG_SEPARATOR;
		start = next;
	}
	return start;
}

static struct token *dup_token(struct token *token, struct token *pos, int newline)
{
	struct token *alloc = alloc_token(pos);
	alloc->type = token->type;
	alloc->line = pos->line;
	alloc->newline = newline;
	alloc->integer = token->integer;
	return alloc;	
}

static void insert(struct token *token, struct token *prev)
{
	token->next = prev->next;
	prev->next = token;
}

static struct token * replace(struct token *token, struct token *prev, struct token *list)
{
	int newline = token->newline;

	prev->next = token->next;
	while (!eof_token(list) && !match_op(list, SPECIAL_ARG_SEPARATOR)) {
		struct token *newtok = dup_token(list, token, newline);
		newline = 0;
		insert(newtok, prev);
		prev = newtok;
		list = list->next;
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
	struct token *newtoken = alloc_token(token);
	struct string *string = __alloc_string(size);

	newtoken->newline = token->newline;
	memcpy(string->data, s, size);
	string->length = size;
	newtoken->type = TOKEN_STRING;
	newtoken->string = string;
	newtoken->next = &eof_token_entry;
	return newtoken;
}

static int arg_number(struct token *arglist, struct ident *ident)
{
	int nr = 0;

	while (!eof_token(arglist)) {
		if (arglist->ident == ident)
			return nr;
		nr++;
		arglist = arglist->next;
	}
	return -1;
}			

static struct token empty_arg_token = { .type = TOKEN_EOF };

static struct token *expand_one_arg(struct token *head, struct token *token,
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

		head = replace(token, head, arg);
		if (!match_op(orig_head, SPECIAL_HASHHASH) && !match_op(last, SPECIAL_HASHHASH) && !match_op(orig_head, '#'))
			head = expand_list(orig_head);
		head->next = last;
		return head;
	}
	return token;
}

static void expand_arguments(struct token *token, struct token *head,
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
			if (nextnext != head && nr >= 0 && nextnext->type == TOKEN_IDENT) {
				struct token *newtoken = stringify(nextnext, get_argument(nr, arguments));
				replace(nextnext, head, newtoken);
				continue;
			}
			warn(next, "'#' operation is not followed by argument name");
		}

		if (next->type == TOKEN_IDENT)
			next = expand_one_arg(head, next, arglist, arguments);

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
	if (second->type == TOKEN_EOF) {
		head->next = second->next;
		return head;
	}

	p = buffer;
	switch (first->type) {
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

	switch (second->type) {
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

	newtoken = alloc_token(first);
	head->next = newtoken;
	newtoken->type = first->type;
	switch (newtoken->type) {
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

static struct token *expand(struct token *head, struct symbol *sym)
{
	struct token *arguments, *token, *last;

	sym->busy++;
	token = head->next;
	last = token->next;

	arguments = NULL;
	if (sym->arglist) {
		arguments = last->next;
		last = find_argument_end(last);
	}
	token->next = &eof_token_entry;

	/* Replace the token with the token expansion */
	replace(token, head, sym->expansion);

	/* Then, replace all the arguments with their expansions */
	if (arguments)
		expand_arguments(token, head, arguments, sym->arglist);

	/* Re-tokenize the sequence if any ## token exists.. */
	retokenize(head);

	/* Finally, expand the expansion itself .. */
	head = expand_list(head);

	/* Put the rest of the stuff in place again */
	head->next = last;
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
			close(fd);
			return;
		}
	}
	warn(token, "unable to open '%s'", filename);
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
		expand_list(token);
		expect = 0;
		next = token;
	}
	token = next->next;
	filename = token_name_sequence(token, expect, token);
	do_include(head, token, filename);
	return 1;
}

static int token_list_different(struct token *list1, struct token *list2)
{
	for (;;) {
		if (list1 == list2)
			return 0;
		if (!list1 || !list2)
			return 1;
		if (list1->type != list2->type)
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

	if (left->type != TOKEN_IDENT) {
		warn(head, "expected identifier to 'define'");
		return 0;
	}
	if (false_nesting)
		return 1;
	name = left->ident;

	arglist = NULL;
	expansion = left->next;
	if (!expansion->whitespace && match_op(expansion, '(')) {
		arglist = expansion;
		while (!eof_token(expansion)) {
			struct token *next = expansion->next;
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
			warn(left, "preprocessor token redefined");
			warn(sym->token, "this was the original definition");
		}
		return 1;
	}
	sym = alloc_symbol(left, SYM_NODE);
	bind_symbol(sym, name, NS_PREPROCESSOR);

	sym->expansion = expansion;
	sym->arglist = arglist;
	return 1;
}

static int handle_undef(struct stream *stream, struct token *head, struct token *token)
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

static int handle_ifdef(struct stream *stream, struct token *head, struct token *token)
{
	return preprocessor_if(token, token_defined(token->next));
}

static int handle_ifndef(struct stream *stream, struct token *head, struct token *token)
{
	struct token *next = token->next;
	if (stream->constant == -1) {
		int newconstant = 0;
		if (next->type == TOKEN_IDENT) {
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

	expand_defined(head);
	expand_list(head);
	token = constant_expression(head->next, &expr);
	if (!eof_token(token))
		warn(token, "garbage at end: %s", show_token_sequence(token));
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
	warn(token, "unmatched '#elif'");
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
	warn(token, "unmatched #else");
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
	warn(token, "unmatched #endif");
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
		whitespace = token->whitespace;
	}
	*ptr = 0;
	return buffer;
}

static int handle_warning(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	warn(token, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_error(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	error(token, "%s", show_token_sequence(token->next));
	return 1;
}

static int handle_nostdinc(struct stream *stream, struct token *head, struct token *token)
{
	if (false_nesting)
		return 1;
	includepath[1] = NULL;
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
	warn(token, "too many include path entries");
}

static int handle_add_include(struct stream *stream, struct token *head, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token->type != TOKEN_STRING) {
			warn(token, "expected path string");
			return 1;
		}
		add_path_entry(token, token->string->data);
	}
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

	if (token->type == TOKEN_IDENT)
		if (handle_preprocessor_command(stream, head, token->ident, token))
			return;
	warn(token, "unrecognized preprocessor line '%s'", show_token_sequence(token));
}

static void preprocessor_line(struct stream *stream, struct token * head)
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
	handle_preprocessor_line(stream, head, start->next);
}

static void do_preprocess(struct token *head)
{
	do {
		struct token *next = head->next;
		struct stream *stream = input_streams + next->stream;

		if (next->newline && match_op(next, '#')) {
			preprocessor_line(stream, head);
			continue;
		}

		if (false_nesting) {
			head->next = next->next;
			continue;
		}

		switch (next->type) {
		case TOKEN_STREAMEND:
			if (stream->constant == -1 && stream->protect) {
				stream->constant = 1;
			}
			/* fallthrough */
		case TOKEN_STREAMBEGIN:
			head->next = next->next;
			continue;

		case TOKEN_IDENT:
			next = expand_one_symbol(head, next);
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
	struct token header = { 0, };

	header.next = token;
	do_preprocess(&header);
	if (if_nesting)
		warn(unmatched_if, "unmatched preprocessor conditional");

	// Drop all expressions from pre-processing, they're not used any more.
	clear_expression_alloc();

	return header.next;
}
