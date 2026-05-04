/*
 * Do C preprocessing, based on a token list gathered by
 * the tokenizer.
 *
 * This may not be the smartest preprocessor on the planet.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
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
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "lib.h"
#include "allocate.h"
#include "parse.h"
#include "token.h"
#include "symbol.h"
#include "expression.h"
#include "scope.h"

static struct ident_list *macros;	// only needed for -dD
static int false_nesting = 0;
static int counter_macro = 0;		// __COUNTER__ expansion
static int include_level = 0;
static int expanding = 0;

#define INCLUDEPATHS 300
const char *includepath[INCLUDEPATHS+1] = {
	"",
	"/usr/include",
	"/usr/local/include",
	NULL
};

static const char **quote_includepath = includepath;
static const char **angle_includepath = includepath + 1;
static const char **isys_includepath   = includepath + 1;
static const char **sys_includepath   = includepath + 1;
static const char **dirafter_includepath = includepath + 3;

#define dirty_stream(stream)				\
	do {						\
		if (!stream->dirty) {			\
			stream->dirty = 1;		\
			if (!stream->ifndef)		\
				stream->protect = NULL;	\
		}					\
	} while(0)

#define end_group(stream)					\
	do {							\
		if (stream->ifndef == stream->top_if) {		\
			stream->ifndef = NULL;			\
			if (!stream->dirty)			\
				stream->protect = NULL;		\
			else if (stream->protect)		\
				stream->dirty = 0;		\
		}						\
	} while(0)

#define nesting_error(stream)		\
	do {				\
		stream->dirty = 1;	\
		stream->ifndef = NULL;	\
		stream->protect = NULL;	\
	} while(0)

static struct token *alloc_token(struct position *pos)
{
	struct token *token = __alloc_token(0);

	token->pos.stream = pos->stream;
	token->pos.line = pos->line;
	token->pos.pos = pos->pos;
	token->pos.whitespace = 1;
	return token;
}

/* Expand symbol 'sym' at '*list' */
static int expand(struct token **, struct symbol *);

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
	char *buf = __alloc_bytes(11);
	sprintf(buf, "%u", val);
	token_type(token) = TOKEN_NUMBER;
	token->number = buf;
}

static struct symbol *lookup_macro(struct ident *ident)
{
	struct symbol *sym = lookup_symbol(ident, NS_MACRO | NS_UNDEF);
	if (sym && !(sym->namespace & NS_MACRO))
		sym = NULL;
	return sym;
}

static int token_defined(struct token *token)
{
	if (token_type(token) == TOKEN_IDENT) {
		struct symbol *sym = lookup_macro(token->ident);
		if (sym) {
			sym->used_in = file_scope;
			return 1;
		}
		return 0;
	}

	sparse_error(token->pos, "expected preprocessor identifier");
	return 0;
}

static void replace_with_bool(struct token *token, bool val)
{
	static const char *string[] = { "0", "1" };

	token_type(token) = TOKEN_NUMBER;
	token->number = string[val];
}

static void replace_with_defined(struct token *token)
{
	replace_with_bool(token, token_defined(token));
}

static void expand_line(struct token *token)
{
	replace_with_integer(token, token->pos.line);
}

static void expand_file(struct token *token)
{
	replace_with_string(token, stream_name(token->pos.stream));
}

static void expand_basefile(struct token *token)
{
	replace_with_string(token, base_filename);
}

static time_t t = 0;
static void expand_date(struct token *token)
{
	static char buffer[12]; /* __DATE__: 3 + ' ' + 2 + ' ' + 4 + '\0' */

	if (!t)
		time(&t);
	strftime(buffer, 12, "%b %e %Y", localtime(&t));
	replace_with_string(token, buffer);
}

static void expand_time(struct token *token)
{
	static char buffer[9]; /* __TIME__: 2 + ':' + 2 + ':' + 2 + '\0' */

	if (!t)
		time(&t);
	strftime(buffer, 9, "%T", localtime(&t));
	replace_with_string(token, buffer);
}

static void expand_counter(struct token *token)
{
	replace_with_integer(token, counter_macro++);
}

static void expand_include_level(struct token *token)
{
	replace_with_integer(token, include_level - 1);
}

static inline int expand_one_symbol(struct token **list)
{
	struct token *token = *list;
	struct symbol *sym;

	if (token->pos.noexpand)
		return 1;

	sym = lookup_macro(token->ident);
	if (!sym)
		return 1;
	store_macro_pos(token);
	if (sym->expand_simple) {
		sym->expand_simple(token);
		return 1;
	} else {
		int rc;

		sym->used_in = file_scope;
		expanding = 1;
		rc = expand(list, sym);
		expanding = 0;
		return rc;
	}
}

static inline struct token *scan_next(struct token **where)
{
	struct token *token = *where;
	if (token_type(token) != TOKEN_UNTAINT)
		return token;
	do {
		token->ident->tainted = 0;
		token = token->next;
	} while (token_type(token) == TOKEN_UNTAINT);
	*where = token;
	return token;
}

static void expand_list(struct token **list)
{
	struct token *next;
	while (!eof_token(next = scan_next(list))) {
		if (token_type(next) != TOKEN_IDENT || expand_one_symbol(list))
			list = &next->next;
	}
}

static void preprocessor_line(struct stream *stream, struct token **line);

static struct token *collect_arg(struct token *prev, bool vararg, const struct position *pos)
{
	struct stream *stream = input_streams + prev->pos.stream;
	struct token **p = &prev->next;
	struct token *next;
	int nesting = 0;

	while (!eof_token(next = scan_next(p))) {
		if (next->pos.newline && match_op(next, '#')) {
			if (!next->pos.noexpand) {
				preprocessor_line(stream, p);
				__free_token(next);	/* Free the '#' token */
				continue;
			}
		}
		switch (token_type(next)) {
		case TOKEN_STREAMEND:
		case TOKEN_STREAMBEGIN:
			*p = &eof_token_entry;
			return next;
		}
		if (false_nesting) {
			*p = next->next;
			__free_token(next);
			continue;
		}
		if (match_op(next, '(')) {
			nesting++;
		} else if (match_op(next, ')')) {
			if (!nesting--)
				break;
		} else if (match_op(next, ',') && !nesting && !vararg) {
			break;
		}
		next->pos.stream = pos->stream;
		next->pos.line = pos->line;
		next->pos.pos = pos->pos;
		next->pos.newline = 0;
		p = &next->next;
	}
	*p = &eof_token_entry;
	return next;
}

/*
 * We store arglist as <counter> [arg1] <number of uses for arg1> ... eof
 */

struct arg {
	struct token *arg[3];
};

static int collect_arguments(struct token *what, int fixed, bool vararg, struct arg *args)
{
	struct token *start = scan_next(&what->next);
	struct token *next = NULL, *v = NULL;
	const char *err;
	int commas;

	memset(args, 0, sizeof(struct arg) * (fixed + 1));

	if (!match_op(start, '('))
		return 0;
	for (commas = 0; commas < fixed; commas++) {
		next = collect_arg(start, false, &what->pos);
		if (token_type(next) != TOKEN_SPECIAL)
			goto Eclosing;
		args[commas + 1].arg[ARG_QUOTED] = start->next;
		if (!match_op(next, ',')) {
			if (commas < fixed - 1)
				goto Efew;
			break;
		}
		start = next;
	}
	if (commas == fixed) {
		next = collect_arg(start, true, &what->pos);
		if (token_type(next) != TOKEN_SPECIAL)
			goto Eclosing;
		v = start->next;
		if (fixed == 0 && eof_token(v))
			v = NULL;
	}
	if (v && !vararg)
		goto Eexcess;
	if (vararg)
		args[0].arg[ARG_QUOTED] = v;
	what->next = next->next;
	return 1;

Efew:
	err = "too few arguments provided to";
	next = next->next;
	goto out;
Eexcess:
	err = "too many arguments provided to";
	next = next->next;
	goto out;
Eclosing:
	err = "unterminated argument list invoking";
out:
	sparse_error(what->pos, "%s macro \"%s\"", err, show_ident(what->ident));
	what->next = next;
	return 0;
}

static struct token *dup_list(struct token *list)
{
	struct token *res = NULL;
	struct token **p = &res;

	while (!eof_token(list)) {
		struct token *newtok = __alloc_token(0);
		*newtok = *list;
		*p = newtok;
		p = &newtok->next;
		list = list->next;
	}
	return res;
}

static const char *show_token_sequence(struct token *token, int quote)
{
	static char buffer[MAX_STRING];
	char *ptr = buffer;
	int whitespace = 0;

	if (!token && !quote)
		return "<none>";
	while (!eof_token(token)) {
		const char *val = quote ? quote_token(token) : show_token(token);
		int len = strlen(val);

		if (ptr + whitespace + len >= buffer + sizeof(buffer)) {
			sparse_error(token->pos, "too long token expansion");
			break;
		}

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

static struct token *stringify(struct token *arg)
{
	const char *s = show_token_sequence(arg, 1);
	int size = strlen(s)+1;
	struct token *token = __alloc_token(0);
	struct string *string = __alloc_string(size);

	memcpy(string->data, s, size);
	string->length = size;
	token->pos = arg->pos;
	token_type(token) = TOKEN_STRING;
	token->string = string;
	token->next = &eof_token_entry;
	return token;
}

static struct token *empty_string(const struct position *pos)
{
	struct token *token = __alloc_token(0);
	static struct string empty = {.immutable = 1, .length = 1, .data = ""};

	token->pos = *pos;
	token_type(token) = TOKEN_STRING;
	token->string = &empty;
	token->next = &eof_token_entry;
	return token;
}

/*
 * Possibly valid combinations:
 *  - ident + ident -> ident
 *  - ident + number -> ident unless number contains '.', '+' or '-'.
 *  - 'L' + char constant -> wide char constant
 *  - 'L' + string literal -> wide string literal
 *  - number + number -> number
 *  - number + ident -> number
 *  - number + '.' -> number
 *  - number + '+' or '-' -> number, if number used to end on [eEpP].
 *  - '.' + number -> number, if number used to start with a digit.
 *  - special + special -> either special or an error.
 */
static enum token_type combine(struct token *left, struct token *right, char *p)
{
	int len;
	enum token_type t1 = token_type(left), t2 = token_type(right);

	if (t1 != TOKEN_IDENT && t1 != TOKEN_NUMBER && t1 != TOKEN_SPECIAL)
		return TOKEN_ERROR;

	if (t1 == TOKEN_IDENT && left->ident == &L_ident) {
		if (t2 >= TOKEN_CHAR && t2 < TOKEN_WIDE_CHAR)
			return t2 + TOKEN_WIDE_CHAR - TOKEN_CHAR;
		if (t2 == TOKEN_STRING)
			return TOKEN_WIDE_STRING;
	}

	if (t2 != TOKEN_IDENT && t2 != TOKEN_NUMBER && t2 != TOKEN_SPECIAL)
		return TOKEN_ERROR;

	strcpy(p, show_token(left));
	strcat(p, show_token(right));
	len = strlen(p);

	if (len >= 256)
		return TOKEN_ERROR;

	if (t1 == TOKEN_IDENT) {
		if (t2 == TOKEN_SPECIAL)
			return TOKEN_ERROR;
		if (t2 == TOKEN_NUMBER && strpbrk(p, "+-."))
			return TOKEN_ERROR;
		return TOKEN_IDENT;
	}

	if (t1 == TOKEN_NUMBER) {
		if (t2 == TOKEN_SPECIAL) {
			switch (right->special) {
			case '.':
				break;
			case '+': case '-':
				if (strchr("eEpP", p[len - 2]))
					break;
			default:
				return TOKEN_ERROR;
			}
		}
		return TOKEN_NUMBER;
	}

	if (p[0] == '.' && isdigit((unsigned char)p[1]))
		return TOKEN_NUMBER;

	return TOKEN_SPECIAL;
}

static int merge(struct token *left, struct token *right)
{
	static char buffer[512];
	enum token_type res = combine(left, right, buffer);
	int n;

	switch (res) {
	case TOKEN_IDENT:
		left->ident = built_in_ident(buffer);
		left->pos.noexpand = left->ident->tainted;
		return 1;

	case TOKEN_NUMBER:
		token_type(left) = TOKEN_NUMBER;	/* could be . + num */
		left->number = xstrdup(buffer);
		return 1;

	case TOKEN_SPECIAL:
		if (buffer[2] && buffer[3])
			break;
		for (n = SPECIAL_BASE; n < SPECIAL_ARG_SEPARATOR; n++) {
			if (!memcmp(buffer, combinations[n-SPECIAL_BASE], 3)) {
				left->special = n;
				return 1;
			}
		}
		break;

	case TOKEN_WIDE_CHAR:
	case TOKEN_WIDE_STRING:
		token_type(left) = res;
		left->string = right->string;
		return 1;

	case TOKEN_WIDE_CHAR_EMBEDDED_0 ... TOKEN_WIDE_CHAR_EMBEDDED_3:
		token_type(left) = res;
		memcpy(left->embedded, right->embedded, 4);
		return 1;

	default:
		;
	}
	sparse_error(left->pos, "'##' failed: concatenation is not a valid token");
	return 0;
}

static inline struct token *dup_token(const struct token *token, struct position *streampos)
{
	struct position pos = *streampos;
	struct token *alloc = __alloc_token(0);
	struct position pos2 = token->pos;

	alloc->ident = token->ident;
	pos2.stream = pos.stream;
	pos2.line = pos.line;
	pos2.pos = pos.pos;
	if (pos2.type == TOKEN_STRING || pos2.type == TOKEN_WIDE_STRING)
		token->string->immutable = 1;
	if (pos2.type == TOKEN_IDENT && token->ident->tainted)
		pos2.noexpand = 1;
	alloc->pos = pos2;
	return alloc;	
}

static struct token **move_into(struct token **where, struct token *list)
{
	*where = list;
	while (!eof_token(list)) {
		if (token_type(list) == TOKEN_IDENT && list->ident->tainted)
			list->pos.noexpand = 1;
		where = &list->next;
		list = *where;
	}
	return where;
}

static struct token **copy(struct token **where, struct token *list)
{
	while (!eof_token(list)) {
		struct position pos = list->pos;
		struct token *token = __alloc_token(0);

		token->ident = list->ident;
		if (pos.type == TOKEN_STRING || pos.type == TOKEN_WIDE_STRING)
			list->string->immutable = 1;
		if (pos.type == TOKEN_IDENT && list->ident->tainted)
			pos.noexpand = 1;
		token->pos = pos;
		*where = token;
		where = &token->next;
		list = list->next;
	}
	*where = &eof_token_entry;
	return where;
}

static inline int argnum(const struct token *arg)
{
	return arg->argnum >> ARGNUM_BITS_STOLEN;
}

static inline enum arg_kind argkind(const struct token *arg)
{
	return arg->argnum & ARGNUM_KIND_MASK;
}

static int handle_kludge(const struct token **p, struct arg *args)
{
	const struct token *t = (*p)->next->next;
	while (1) {
		struct token *v = args[argnum(t)].arg[ARG_QUOTED];
		if (token_type(t->next) != TOKEN_CONCAT) {
			if (v) {
				/* ignore the first ## */
				*p = (*p)->next;
				return 0;
			}
			/* skip the entire thing */
			*p = t;
			return 1;
		}
		if (v && !eof_token(v))
			return 0; /* no magic */
		t = t->next->next;
	}
}

static struct token *do_argument(const struct token *body,
				 struct arg *args,
				 struct ident *expanding)
{
	struct token *arg = args[argnum(body)].arg[argkind(body)];
	if (arg)
		return arg;
	arg = args[argnum(body)].arg[ARG_QUOTED];
	if (!arg)
		arg = &eof_token_entry;
	if (argkind(body) == ARG_NORMAL) {
		if (!eof_token(arg)) {
			if (!(body->argnum & (1 << ARGNUM_CONSUME_EXPAND)))
				arg = dup_list(arg);
			expanding->tainted = 0;
			expand_list(&arg);
			expanding->tainted = 1;
		}
		return args[argnum(body)].arg[ARG_NORMAL] = arg;
	}
	if (argkind(body) == ARG_STR)
		return args[argnum(body)].arg[ARG_STR] = stringify(arg);
	return arg;	// ARG_QUOTED
}

static bool is_end_va_opt(const struct token *token)
{
	return eof_token(token->next);
}

static bool skip_va_opt(struct arg *args, struct ident *expanding)
{
	struct token *arg = args[0].arg[ARG_NORMAL];
	if (arg)
		return eof_token(arg);
	arg = args[0].arg[ARG_QUOTED];
	if (!arg || eof_token(arg))
		return true;
	arg = dup_list(arg);
	expanding->tainted = 0;
	expand_list(&arg);
	expanding->tainted = 1;
	args[0].arg[ARG_NORMAL] = arg;
	return eof_token(arg);
}

static struct token **substitute(struct token **list, const struct token *body, struct arg *args)
{
	struct position *base_pos = &(*list)->pos;
	enum {Normal, Placeholder, Concat} state = Normal, saved_state = Normal;
	struct ident *expanding = (*list)->ident;
	struct token **saved_list = NULL, *va_opt_list;

	expanding->tainted = 1;

	for (; !eof_token(body); body = body->next) {
		struct token *added;

		if (token_type(body) <= TOKEN_LAST_NORMAL) {
			added = dup_token(body, base_pos);
		} else if (token_type(body) == TOKEN_MACRO_ARGUMENT) {
			struct token **inserted_at;
			struct token *arg;

			arg = do_argument(body, args, expanding);
			if (!arg || eof_token(arg)) {
				if (state == Concat)
					state = Normal;
				else
					state = Placeholder;
				continue;
			}
			if (state == Concat && merge(containing_token(list), arg)) {
				arg = arg->next;
				if (eof_token(arg)) {
					// merged the sole token in
					state = Normal;
					continue;
				}
				inserted_at = NULL;
			} else {
				inserted_at = list;
			}
			if (body->argnum & (1 << ARGNUM_CONSUME))
				list = move_into(list, arg);
			else
				list = copy(list, arg);
			if (inserted_at) {
				struct token *p = *inserted_at;
				p->pos.whitespace = body->pos.whitespace;
				p->pos.newline = 0;
			}
			state = Normal;
			continue;
		} else if (token_type(body) == TOKEN_CONCAT) {
			if (state == Placeholder)
				state = Normal;
			else
				state = Concat;
			continue;
		} else if (token_type(body) == TOKEN_GNU_KLUDGE) {
			const struct token *t = body;
			/*
			 * GNU kludge: if we had <comma>##<vararg>, behaviour
			 * depends on whether we had enough arguments to have
			 * a vararg.  If we did, ## is just ignored.  Otherwise
			 * both , and ## are ignored.  Worse, there can be
			 * an arbitrary number of ##<arg> in between; if all of
			 * those are empty, we act as if they hadn't been there,
			 * otherwise we act as if the kludge didn't exist.
			 */
			if (handle_kludge(&body, args)) {
				if (state == Concat)
					state = Normal;
				else
					state = Placeholder;
				continue;
			}
			added = dup_token(t, base_pos);
			token_type(added) = TOKEN_SPECIAL;
		} else if (token_type(body) == TOKEN_VA_OPT) {
			// entering va_opt?
			if (!is_end_va_opt(body)) {
				if (skip_va_opt(args, expanding)) {
					if (state == Concat)
						state = Normal;
					else
						state = Placeholder;
					continue;
				}
				body = body->va_opt_linkage;
				continue;
			}
			body = body->va_opt_linkage;
			// leaving va_opt?
			if (token_type(body) == TOKEN_VA_OPT)
				continue;
			// leaving #va_opt
			if (list == &va_opt_list) {
				added = empty_string(base_pos);
			} else {
				*list = &eof_token_entry;
				added = stringify(va_opt_list);
			}
			list = saved_list;
			state = saved_state;
		} else if (token_type(body) == TOKEN_VA_OPT_STR) {
			// entering #va_opt
			if (!skip_va_opt(args, expanding)) {
				saved_state = state;
				state = Normal;
				saved_list = list;
				list = &va_opt_list;
				body = body->va_opt_linkage;
				continue;
			}
			added = empty_string(base_pos);
		} else {
			sparse_error(body->pos, "bad token type(%d)", token_type(body));
			break;
		}

		/*
		 * if we got to doing real concatenation, we already have
		 * added something into the list, so containing_token() is OK.
		 */
		if (state != Concat || !merge(containing_token(list), added)) {
			*list = added;
			list = &added->next;
		}
		state = Normal;
	}
	return list;
}

static int expand(struct token **list, struct symbol *sym)
{
	struct token *next;
	struct token *token = *list;
	struct token **tail;
	struct token *expansion = sym->expansion;
	struct arg args[sym->fixed_args + 1];

	if (sym->arglist &&
	    !collect_arguments(token, sym->fixed_args, sym->vararg, args))
		return 1;

	if (sym->expand)
		return sym->expand(token, args) ? 0 : 1;

	next = token->next;
	tail = substitute(list, expansion, args);
	/*
	 * Note that it won't be eof - at least TOKEN_UNTAINT will be there.
	 * We still can lose the newline flag if the sucker expands to nothing,
	 * but the price of dealing with that is probably too high (we'd need
	 * to collect the flags during scan_next())
	 */
	(*list)->pos.newline = token->pos.newline;
	(*list)->pos.whitespace = token->pos.whitespace;
	*tail = next;

	return 0;
}

static const char *token_name_sequence(struct token *token, int endop, struct token *start)
{
	static char buffer[256];
	char *ptr = buffer;

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
		sparse_error(start->pos, "expected '>' at end of filename");
	return buffer;
}

static int already_tokenized(const char *path)
{
	int stream, next;

	for (stream = *hash_stream(path); stream >= 0 ; stream = next) {
		struct stream *s = input_streams + stream;

		next = s->next_stream;
		if (s->once) {
			if (strcmp(path, s->name))
				continue;
			return 1;
		}
		if (s->constant != CONSTANT_FILE_YES)
			continue;
		if (strcmp(path, s->name))
			continue;
		if (s->protect && !lookup_macro(s->protect))
			continue;
		return 1;
	}
	return 0;
}

/* Handle include of header files.
 * The relevant options are made compatible with gcc. The only options that
 * are not supported is -withprefix and friends.
 *
 * Three set of include paths are known:
 * quote_includepath:	Path to search when using #include "file.h"
 * angle_includepath:	Paths to search when using #include <file.h>
 * isys_includepath:	Paths specified with -isystem, come before the
 *			built-in system include paths. Gcc would suppress
 *			warnings from system headers. Here we separate
 *			them from the angle_ ones to keep search ordering.
 *
 * sys_includepath:	Built-in include paths.
 * dirafter_includepath Paths added with -dirafter.
 *
 * The above is implemented as one array with pointers
 *                         +--------------+
 * quote_includepath --->  |              |
 *                         +--------------+
 *                         |              |
 *                         +--------------+
 * angle_includepath --->  |              |
 *                         +--------------+
 * isys_includepath  --->  |              |
 *                         +--------------+
 * sys_includepath   --->  |              |
 *                         +--------------+
 * dirafter_includepath -> |              |
 *                         +--------------+
 *
 * -I dir insert dir just before isys_includepath and move the rest
 * -I- makes all dirs specified with -I before to quote dirs only and
 *   angle_includepath is set equal to isys_includepath.
 * -nostdinc removes all sys dirs by storing NULL in entry pointed
 *   to by * sys_includepath. Note that this will reset all dirs built-in
 *   and added before -nostdinc by -isystem and -idirafter.
 * -isystem dir adds dir where isys_includepath points adding this dir as
 *   first systemdir
 * -idirafter dir adds dir to the end of the list
 */

static void set_stream_include_path(struct stream *stream)
{
	const char *path = stream->path;
	if (!path) {
		const char *p = strrchr(stream->name, '/');
		path = "";
		if (p) {
			int len = p - stream->name + 1;
			char *m = malloc(len+1);
			/* This includes the final "/" */
			memcpy(m, stream->name, len);
			m[len] = 0;
			path = m;
			/* normalize this path */
			while (path[0] == '.' && path[1] == '/') {
				path += 2;
				while (path[0] == '/')
					path++;
			}
		}
		stream->path = path;
	}
	includepath[0] = path;
}

static int try_include(struct position pos, const char *path, const char *filename, int flen, struct token **where, const char **next_path)
{
	int fd;
	int plen = strlen(path);
	static char fullname[PATH_MAX];

	memcpy(fullname, path, plen);
	if (plen && path[plen-1] != '/') {
		fullname[plen] = '/';
		plen++;
	}
	memcpy(fullname+plen, filename, flen);
	if (already_tokenized(fullname))
		return 1;
	fd = open(fullname, O_RDONLY);
	if (fd >= 0) {
		char *streamname = xmemdup(fullname, plen + flen);
		*where = tokenize(&pos, streamname, fd, *where, next_path);
		close(fd);
		return 1;
	}
	return 0;
}

static int do_include_path(const char **pptr, struct token **list, struct token *token, const char *filename, int flen)
{
	const char *path;

	while ((path = *pptr++) != NULL) {
		if (!try_include(token->pos, path, filename, flen, list, pptr))
			continue;
		return 1;
	}
	return 0;
}

static int free_preprocessor_line(struct token *token)
{
	while (token_type(token) != TOKEN_EOF) {
		struct token *free = token;
		token = token->next;
		__free_token(free);
	};
	return 1;
}

const char *find_include(const char *skip, const char *look_for)
{
	DIR *dp;
	struct dirent *entry;
	struct stat statbuf;
	const char *ret;
	char cwd[PATH_MAX];
	static char buf[PATH_MAX + 1];

	dp = opendir(".");
	if (!dp)
		return NULL;

	if (!getcwd(cwd, sizeof(cwd)))
		goto close;

	while ((entry = readdir(dp))) {
		lstat(entry->d_name, &statbuf);

		if (strcmp(entry->d_name, look_for) == 0) {
			int cnt;

			cnt = snprintf(buf, sizeof(buf), "%s/%s", cwd, entry->d_name);
			if (cnt >= sizeof(buf))
				return NULL;
			closedir(dp);
			return buf;
		}

		if (S_ISDIR(statbuf.st_mode)) {
			/* Found a directory, but ignore . and .. */
			if (strcmp(".", entry->d_name) == 0 ||
			    strcmp("..", entry->d_name) == 0 ||
			    strcmp(skip, entry->d_name) == 0)
				continue;

			chdir(entry->d_name);
			ret = find_include("", look_for);
			chdir("..");
			if (ret) {
				closedir(dp);
				return ret;
			}
		}
	}
close:
	closedir(dp);

	return NULL;
}

const char *search_dir(const char *stop, const char *look_for)
{
	char cwd[PATH_MAX];
	int len;
	const char *ret;
	int cnt = 0;

	if (!getcwd(cwd, sizeof(cwd)))
		return NULL;

	len = strlen(cwd);
	while (len >= 0) {
		ret = find_include(cnt++ ? cwd + len + 1 : "", look_for);
		if (ret)
			return ret;

		if (strcmp(cwd, stop) == 0 ||
		    strcmp(cwd, "/usr/include") == 0 ||
		    strcmp(cwd, "/usr/local/include") == 0 ||
		    strlen(cwd) <= 10 ||  /* heck...  don't search /usr/lib/ */
		    strcmp(cwd, "/") == 0)
			return NULL;

		while (--len >= 0) {
			if (cwd[len] == '/') {
				cwd[len] = '\0';
				break;
			}
		}

		chdir("..");
	}
	return NULL;
}

static void use_best_guess_header_file(struct token *token, const char *filename, struct token **list)
{
	char cwd[PATH_MAX];
	char dir_part[PATH_MAX];
	const char *file_part;
	const char *include_name;
	static int cnt;
	int len;

	/* Avoid guessing includes recursively. */
	if (cnt++ > 1000)
		return;

	if (!filename || filename[0] == '\0')
		return;

	file_part = filename;
	while ((filename = strchr(filename, '/'))) {
		++filename;
		if (filename[0])
			file_part = filename;
	}

	snprintf(dir_part, sizeof(dir_part), "%s", stream_name(token->pos.stream));
	len = strlen(dir_part);
	while (--len >= 0) {
		if (dir_part[len] == '/') {
			dir_part[len] = '\0';
			break;
		}
	}
	if (len < 0)
		sprintf(dir_part, ".");

	if (!getcwd(cwd, sizeof(cwd)))
		return;

	chdir(dir_part);
	include_name = search_dir(cwd, file_part);
	chdir(cwd);
	if (!include_name)
		return;
	sparse_error(token->pos, "using '%s'", include_name);

	try_include(token->pos, "", include_name, strlen(include_name), list, includepath);
}

static int handle_include_path(struct stream *stream, struct token **list, struct token *token, int how)
{
	const char *filename;
	struct token *next;
	const char **path;
	int expect;
	int flen;

	next = token->next;
	expect = '>';
	if (!match_op(next, '<')) {
		expand_list(&token->next);
		expect = 0;
		next = token;
		if (match_op(token->next, '<')) {
			next = token->next;
			expect = '>';
		}
	}

	token = next->next;
	filename = token_name_sequence(token, expect, token);
	flen = strlen(filename) + 1;

	/* Absolute path? */
	if (filename[0] == '/') {
		if (try_include(token->pos, "", filename, flen, list, includepath))
			return 0;
		goto out;
	}

	switch (how) {
	case 1:
		path = stream->next_path;
		break;
	case 2:
		includepath[0] = "";
		path = includepath;
		break;
	default:
		/* Dir of input file is first dir to search for quoted includes */
		set_stream_include_path(stream);
		path = expect ? angle_includepath : quote_includepath;
		break;
	}
	/* Check the standard include paths.. */
	if (do_include_path(path, list, token, filename, flen))
		return 0;
out:
	sparse_error(token->pos, "unable to open '%s'", filename);
	use_best_guess_header_file(token, filename, list);
	return 0;
}

static int handle_include(struct stream *stream, struct token **list, struct token *token)
{
	return handle_include_path(stream, list, token, 0);
}

static int handle_include_next(struct stream *stream, struct token **list, struct token *token)
{
	return handle_include_path(stream, list, token, 1);
}

static int handle_argv_include(struct stream *stream, struct token **list, struct token *token)
{
	return handle_include_path(stream, list, token, 2);
}

static int token_list_different(struct token *, struct token *);

static int token_different(struct token *t1, struct token *t2)
{
	int different;

	if (token_type(t1) != token_type(t2))
		return 1;

	switch (token_type(t1)) {
	case TOKEN_IDENT:
		different = t1->ident != t2->ident;
		break;
	case TOKEN_UNTAINT:
	case TOKEN_CONCAT:
	case TOKEN_GNU_KLUDGE:
		different = 0;
		break;
	case TOKEN_NUMBER:
		different = strcmp(t1->number, t2->number);
		break;
	case TOKEN_SPECIAL:
		different = t1->special != t2->special;
		break;
	case TOKEN_MACRO_ARGUMENT:
		different = t1->argnum != t2->argnum;
		break;
	case TOKEN_CHAR_EMBEDDED_0 ... TOKEN_CHAR_EMBEDDED_3:
	case TOKEN_WIDE_CHAR_EMBEDDED_0 ... TOKEN_WIDE_CHAR_EMBEDDED_3:
		different = memcmp(t1->embedded, t2->embedded, 4);
		break;
	case TOKEN_CHAR:
	case TOKEN_WIDE_CHAR:
	case TOKEN_STRING:
	case TOKEN_WIDE_STRING: {
		struct string *s1, *s2;

		s1 = t1->string;
		s2 = t2->string;
		different = 1;
		if (s1->length != s2->length)
			break;
		different = memcmp(s1->data, s2->data, s1->length);
		break;
	}
	case TOKEN_VA_OPT:
		if (is_end_va_opt(t1)) {
			/*
			 * t1 is a return (at the end of __VA_OPT__ body);
			 * the same should be true for t2 and that's it.
			 */
			different = !is_end_va_opt(t2);
			break;
		}
		/*
		 * t1 is a real __VA_OPT__; the same should be true for
		 * t2...
		 */
		if (is_end_va_opt(t2)) {
			different = 1;
			break;
		}
		/* ... and their bodies should not be different */
		/* fall-through */
	case TOKEN_VA_OPT_STR:
		different = token_list_different(t1->va_opt_linkage,
						 t2->va_opt_linkage);
		break;
	default:
		different = 1;
		break;
	}
	return different;
}

static int token_list_different(struct token *list1, struct token *list2)
{
	for (;;) {
		if (list1 == list2)
			return 0;
		if (!list1 || !list2)
			return 1;
		if (token_different(list1, list2))
			return 1;
		list1 = list1->next;
		list2 = list2->next;
	}
}

static struct ident *macro_arg_name[1024];
static int macro_nargs = 0;
static int macro_vararg = -1;
static bool macro_funclike = false;

static bool macro_add_arg(struct position pos, struct ident *ident)
{
	for (int i = 0; i < macro_nargs; i++) {
		if (ident == macro_arg_name[i])
			goto Edup_arg;
	}
	if (macro_nargs == 1024)
		goto Eargs;
	macro_arg_name[macro_nargs++] = ident;
	return true;
Edup_arg:
	sparse_error(pos, "duplicate macro parameter \"%s\"", show_ident(ident));
	return false;
Eargs:
	sparse_error(pos, "too many arguments in macro definition");
	return false;
}

static void misplaced_va_xxx(struct token *arg)
{
	sparse_error(arg->pos,
	     "%s can only appear in the expansion of a C99 variadic macro",
	     show_token(arg));
}

static struct token *parse_arguments(struct token *list)
{
	struct token *arg = list->next, *next = list;

	if (match_op(arg, ')'))
		return arg;

	while (token_type(arg) == TOKEN_IDENT) {
		if (arg->ident == &__VA_ARGS___ident ||
		    arg->ident == &__VA_OPT___ident)
			goto Eva_args;
		if (!macro_add_arg(arg->pos, arg->ident))
			return NULL;

		next = arg->next;
		if (match_op(next, ',')) {
			arg = next->next;
			continue;
		}

		if (match_op(next, ')'))
			return next;

		/* normal cases are finished here */

		if (match_op(next, SPECIAL_ELLIPSIS)) {
			if (match_op(next->next, ')')) {
				macro_vararg = macro_nargs - 1;
				return next->next;
			}

			arg = next;
			goto Enotclosed;
		}

		if (eof_token(next)) {
			goto Enotclosed;
		} else {
			arg = next;
			goto Ebadstuff;
		}
	}

	if (match_op(arg, SPECIAL_ELLIPSIS)) {
		next = arg->next;
		if (!match_op(next, ')'))
			goto Enotclosed;
		if (!macro_add_arg(arg->pos, &__VA_ARGS___ident))
			return NULL;
		macro_vararg = macro_nargs - 1;
		return next;
	}

	if (eof_token(arg)) {
		arg = next;
		goto Enotclosed;
	}
	if (match_op(arg, ','))
		goto Emissing;
	else
		goto Ebadstuff;


Emissing:
	sparse_error(arg->pos, "parameter name missing");
	return NULL;
Ebadstuff:
	sparse_error(arg->pos, "\"%s\" may not appear in macro parameter list",
		show_token(arg));
	return NULL;
Enotclosed:
	sparse_error(arg->pos, "missing ')' in macro parameter list");
	return NULL;
Eva_args:
	misplaced_va_xxx(arg);
	return NULL;
}

struct arg_state {
	struct token *needs_raw;
	struct token *needs_expanded;
	struct token *needs_str;
	bool seen_uncond_expand;
	bool seen_uncond_str;
};

static bool in_va_opt, seen_va_opt;

static struct token **parse_body(struct token **list, struct arg_state args[]);

static int parse_va_opt(struct token *token, struct arg_state args[])
{
	struct token **p = &token->next;
	struct token *next = *p;
	int nesting = 0;

	if (macro_vararg < 0)
		goto Evararg;
	if (in_va_opt)
		goto Enested;

	if (!match_op(next, '('))
		goto Eunterminated;
	if (!seen_va_opt) {
		/*
		 * The first __VA_OPT__() will need an expanded __VA_ARGS__.
		 * if we had no prior expanded occurrences of __VA_ARGS__,
		 * we'll need its unexpanded form to survive until that point.
		 * Only the cannibalization of unexpended form needs to be
		 * prevented; cannibalization of expanded form doesn't matter.
		 * We only want to know if it's an empty list, i.e. equal to
		 * &eof_token_entry, and the pointer stored in struct args
		 * ->arg[ARG_NORMAL] doesn't change when we get to the last
		 * expanded occurrence of __VA_ARGS__ and consume the list
		 * it's pointing to.
		 */
		if (!args[0].needs_expanded)
			args[0].needs_raw = token;
		seen_va_opt = true;
	}
	token_type(token) = TOKEN_VA_OPT;
	token->va_opt_linkage = next;
	next->next->pos.whitespace = token->pos.whitespace;
	for (; !eof_token(next); p = &next->next, next = *p) {
		if (token_type(next) != TOKEN_SPECIAL)
			continue;
		if (next->special == ')') {
			if (!--nesting) {
				*p = &eof_token_entry; // cut prior to that ')'
				in_va_opt = true;
				p = parse_body(&token->va_opt_linkage->next, args);
				in_va_opt = false;
				if (!p)
					return -1;
				// strip everything up to ')' from the list
				token->next = next->next;
				// convert the ')' into return
				token_type(next) = TOKEN_VA_OPT;
				next->va_opt_linkage = token;
				next->next = &eof_token_entry;
				// and reattach it to the end of body
				*p = next;
				return 0;
			}
		} else if (next->special == '(')
			nesting++;
	}
Eunterminated:
	sparse_error(token->pos, "unterminated __VA_OPT__");
	return -1;

Enested:
	sparse_error(token->pos, "__VA_OPT__ may not appear in a __VA_OPT__");
	return -1;
Evararg:
	misplaced_va_xxx(token);
	return -1;
}

static int check_arg(struct token *token, struct arg_state args[])
{
	struct ident *ident;
	int nr;

	if (!macro_nargs || token_type(token) != TOKEN_IDENT)
		return 0;

	ident = token->ident;
	for (nr = 0; nr < macro_nargs && macro_arg_name[nr] != ident; nr++)
		;

	if (nr < macro_nargs) {
		nr = nr == macro_vararg ? 0 : nr + 1;
		token->argnum = nr << ARGNUM_BITS_STOLEN;
		token_type(token) = TOKEN_MACRO_ARGUMENT;
		return nr + 1;
	}

	if (ident != &__VA_OPT___ident)
		return 0;
	return parse_va_opt(token, args);
}

static void seen_arg(struct token *token, enum arg_kind kind, struct arg_state args[], int nr)
{
	token->argnum |= kind;
	switch (kind) {
	case ARG_QUOTED:
		args[nr].needs_raw = token;
		break;
	case ARG_NORMAL:
		if (!args[nr].seen_uncond_expand &&
		    (!in_va_opt || !args[nr].needs_expanded)) {
			args[nr].seen_uncond_expand = !in_va_opt;
			args[nr].needs_raw = token;
		}
		args[nr].needs_expanded = token;
		break;
	default: // ARG_STR
		if (!args[nr].seen_uncond_str &&
		    (!in_va_opt || !args[nr].needs_str)) {
			args[nr].seen_uncond_str = !in_va_opt;
			args[nr].needs_raw = token;
		}
		args[nr].needs_str = token;
	}
}

static struct token *handle_hash(struct token **p, struct arg_state args[])
{
	struct token *token = *p;
	if (macro_funclike) {
		struct token *next = token->next;
		int nr;

		next->pos.whitespace = token->pos.whitespace;

		nr = check_arg(next, args);
		if (nr < 0)
			return NULL;
		if (token_type(next) == TOKEN_MACRO_ARGUMENT)
			seen_arg(next, ARG_STR, args, nr - 1);
		else if (token_type(next) == TOKEN_VA_OPT)
			token_type(next) = TOKEN_VA_OPT_STR;
		else
			goto Equote;
		__free_token(token);
		token = *p = next;
	} else {
		token->pos.noexpand = 1;
	}
	return token;

Equote:
	sparse_error(token->pos, "'#' is not followed by a macro parameter");
	return NULL;
}

/* token->next is ## */
static struct token *handle_hashhash(struct token *token, struct arg_state args[])
{
	struct token *last = token;
	struct token *concat;
	int state = match_op(token, ',');
	int nr;

	while (1) {
		struct token *t;

		/* eat duplicate ## */
		concat = token->next;
		while (match_op(t = concat->next, SPECIAL_HASHHASH)) {
			token->next = t;
			__free_token(concat);
			concat = t;
		}
		token_type(concat) = TOKEN_CONCAT;

		if (eof_token(t))
			goto Econcat;

		if (match_op(t, '#')) {
			t = handle_hash(&concat->next, args);
			if (!t)
				return NULL;
		}

		nr = check_arg(t, args);
		if (nr < 0)
			return NULL;
		if (nr > 0)
			seen_arg(t, ARG_QUOTED, args, nr - 1);

		if (state == 1 && nr > 0) {
			if (nr == 1)
				state = 2;
		} else {
			last = t;
			state = match_op(t, ',');
		}

		token = t;
		if (!match_op(token->next, SPECIAL_HASHHASH))
			break;
	}
	/* handle GNU ,##__VA_ARGS__ kludge, in all its weirdness */
	if (state == 2)
		token_type(last) = TOKEN_GNU_KLUDGE;
	return token;

Econcat:
	sparse_error(concat->pos, "'##' cannot appear at the ends of macro expansion");
	return NULL;
}

static struct token **parse_body(struct token **list, struct arg_state args[])
{
	struct token *token = *list;

	if (match_op(token, SPECIAL_HASHHASH))
		goto Econcat;

	while (!eof_token(token)) {
		int nr;

		if (match_op(token, '#')) {
			token = handle_hash(list, args);
			if (!token)
				return NULL;
		}
		nr = check_arg(token, args);
		if (nr < 0)
			return NULL;
		if (match_op(token->next, SPECIAL_HASHHASH)) {
			if (nr > 0)
				seen_arg(token, ARG_QUOTED, args, nr - 1);
			token = handle_hashhash(token, args);
			if (!token)
				return NULL;
		} else {
			if (nr > 0)
				seen_arg(token, ARG_NORMAL, args, nr - 1);
		}
		list = &token->next;
		token = *list;
	}
	return list;

Econcat:
	sparse_error(token->pos, "'##' cannot appear at the ends of macro expansion");
	return NULL;
}

static struct token *parse_expansion(struct token *expansion, struct ident *name)
{
	int slots = macro_nargs + (macro_vararg < 0);
	struct arg_state args[slots] = {};
	struct token **tail;
	struct token *token;

	tail = parse_body(&expansion, args);
	seen_va_opt = false;
	if (!tail)
		return NULL;
	for (int i = 0; i < slots; i++) {
		if (args[i].needs_str)
			args[i].needs_str->argnum |= 1 << ARGNUM_CONSUME;
		if (args[i].needs_expanded)
			args[i].needs_expanded->argnum |= 1 << ARGNUM_CONSUME;
		if (args[i].needs_raw) {
			struct token *p = args[i].needs_raw;
			if (token_type(p) != TOKEN_MACRO_ARGUMENT)
				continue;
			if (argkind(p) == ARG_QUOTED)
				p->argnum |= 1 << ARGNUM_CONSUME;
			else if (argkind(p) == ARG_NORMAL)
				p->argnum |= 1 << ARGNUM_CONSUME_EXPAND;
		}
	}
	token = alloc_token(&expansion->pos);
	token_type(token) = TOKEN_UNTAINT;
	token->ident = name;
	token->next = &eof_token_entry;
	*tail = token;
	return expansion;
}

static int do_define(struct position pos, struct token *token, struct ident *name,
		     struct token *arglist, struct token *expansion, int attr)
{
	struct symbol *sym;
	int ret = 1;

	expansion = parse_expansion(expansion, name);
	if (!expansion)
		goto out;

	sym = lookup_symbol(name, NS_MACRO | NS_UNDEF);
	if (sym) {
		int clean;

		if (attr < sym->attr)
			goto out;

		clean = (attr == sym->attr && sym->namespace == NS_MACRO);

		if (token_list_different(sym->expansion, expansion) ||
		    token_list_different(sym->arglist, arglist)) {
			ret = 0;
			if ((clean && attr == SYM_ATTR_NORMAL)
					|| sym->used_in == file_scope) {
				warning(pos, "preprocessor token %.*s redefined",
						name->len, name->name);
				info(sym->pos, "this was the original definition");
			}
		} else if (clean)
			goto out;
	}

	if (!sym || sym->scope != file_scope) {
		sym = alloc_symbol(pos, SYM_NODE);
		bind_symbol(sym, name, NS_MACRO);
		add_ident(&macros, name);
		ret = 0;
	}

	if (!ret) {
		sym->expansion = expansion;
		sym->arglist = arglist;
		sym->vararg = macro_vararg >= 0;
		sym->fixed_args = macro_nargs - sym->vararg;
		if (token) /* Free the "define" token, but not the rest of the line */
			__free_token(token);
	}

	sym->namespace = NS_MACRO;
	sym->used_in = NULL;
	sym->attr = attr;
out:
	macro_nargs = 0;
	macro_vararg = -1;
	macro_funclike = false;
	return ret;
}

///
// predefine a macro with a printf-formatted value
// @name: the name of the macro
// @weak: 0/1 for a normal or a weak define
// @fmt: the printf format followed by it's arguments.
//
// The type of the value is automatically infered:
// TOKEN_NUMBER if it starts by a digit, TOKEN_IDENT otherwise.
// If @fmt is null or empty, the macro is defined with an empty definition.
void predefine(const char *name, int weak, const char *fmt, ...)
{
	struct ident *ident = built_in_ident(name);
	struct token *value = &eof_token_entry;
	int attr = weak ? SYM_ATTR_WEAK : SYM_ATTR_NORMAL;

	if (fmt && fmt[0]) {
		static char buf[256];
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		value = __alloc_token(0);
		if (isdigit((unsigned char)buf[0])) {
			token_type(value) = TOKEN_NUMBER;
			value->number = xstrdup(buf);
		} else {
			token_type(value) = TOKEN_IDENT;
			value->ident = built_in_ident(buf);
		}
		value->pos.whitespace = 1;
		value->next = &eof_token_entry;
	}

	do_define(value->pos, NULL, ident, NULL, value, attr);
}

///
// like predefine() but only if one of the non-standard dialect is chosen
void predefine_nostd(const char *name)
{
	if ((standard & STANDARD_GNU) || (standard == STANDARD_NONE))
		predefine(name, 1, "1");
}

static void predefine_fmt(const char *fmt, int weak, va_list ap)
{
	static char buf[256];

	vsnprintf(buf, sizeof(buf), fmt, ap);
	predefine(buf, weak, "1");
}

void predefine_strong(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	predefine_fmt(fmt, 0, ap);
	va_end(ap);
}

void predefine_weak(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	predefine_fmt(fmt, 1, ap);
	va_end(ap);
}

static int do_handle_define(struct stream *stream, struct token **line, struct token *token, int attr)
{
	struct token *arglist, *expansion;
	struct token *left = token->next;
	struct ident *name;

	if (token_type(left) != TOKEN_IDENT) {
		sparse_error(token->pos, "expected identifier to 'define'");
		return 1;
	}

	name = left->ident;

	arglist = NULL;
	expansion = left->next;
	if (!expansion->pos.whitespace) {
		if (match_op(expansion, '(')) {
			struct token *last = parse_arguments(expansion);
			if (!last) {
				macro_nargs = 0;
				macro_vararg = -1;
				return 1;
			}
			// last points to ) at the end of arguments,
			// expansion starts right after that,
			// everything up to that point is arglist.
			macro_funclike = true;
			arglist = expansion;
			expansion = last->next;
			last->next = &eof_token_entry;
		} else if (!eof_token(expansion)) {
			warning(expansion->pos,
				"no whitespace before object-like macro body");
		}
	}

	return do_define(left->pos, token, name, arglist, expansion, attr);
}

static int handle_define(struct stream *stream, struct token **line, struct token *token)
{
	return do_handle_define(stream, line, token, SYM_ATTR_NORMAL);
}

static int handle_weak_define(struct stream *stream, struct token **line, struct token *token)
{
	return do_handle_define(stream, line, token, SYM_ATTR_WEAK);
}

static int handle_strong_define(struct stream *stream, struct token **line, struct token *token)
{
	return do_handle_define(stream, line, token, SYM_ATTR_STRONG);
}

static int do_handle_undef(struct stream *stream, struct token **line, struct token *token, int attr)
{
	struct token *left = token->next;
	struct symbol *sym;

	if (token_type(left) != TOKEN_IDENT) {
		sparse_error(token->pos, "expected identifier to 'undef'");
		return 1;
	}

	sym = lookup_symbol(left->ident, NS_MACRO | NS_UNDEF);
	if (sym) {
		if (attr < sym->attr)
			return 1;
		if (attr == sym->attr && sym->namespace == NS_UNDEF)
			return 1;
	} else if (attr <= SYM_ATTR_NORMAL)
		return 1;

	if (!sym || sym->scope != file_scope) {
		sym = alloc_symbol(left->pos, SYM_NODE);
		bind_symbol(sym, left->ident, NS_MACRO);
	}

	sym->namespace = NS_UNDEF;
	sym->used_in = NULL;
	sym->attr = attr;

	return 1;
}

static int handle_undef(struct stream *stream, struct token **line, struct token *token)
{
	return do_handle_undef(stream, line, token, SYM_ATTR_NORMAL);
}

static int handle_strong_undef(struct stream *stream, struct token **line, struct token *token)
{
	return do_handle_undef(stream, line, token, SYM_ATTR_STRONG);
}

static int preprocessor_if(struct stream *stream, struct token *token, int cond)
{
	token_type(token) = false_nesting ? TOKEN_SKIP_GROUPS : TOKEN_IF;
	free_preprocessor_line(token->next);
	token->next = stream->top_if;
	stream->top_if = token;
	if (false_nesting || cond != 1)
		false_nesting++;
	return 0;
}

static int handle_ifdef(struct stream *stream, struct token **line, struct token *token)
{
	struct token *next = token->next;
	int arg;
	if (token_type(next) == TOKEN_IDENT) {
		arg = token_defined(next);
	} else {
		dirty_stream(stream);
		if (!false_nesting)
			sparse_error(token->pos, "expected preprocessor identifier");
		arg = -1;
	}
	return preprocessor_if(stream, token, arg);
}

static int handle_ifndef(struct stream *stream, struct token **line, struct token *token)
{
	struct token *next = token->next;
	int arg;
	if (token_type(next) == TOKEN_IDENT) {
		if (!stream->dirty && !stream->ifndef) {
			if (!stream->protect) {
				stream->ifndef = token;
				stream->protect = next->ident;
			} else if (stream->protect == next->ident) {
				stream->ifndef = token;
				stream->dirty = 1;
			}
		}
		arg = !token_defined(next);
	} else {
		dirty_stream(stream);
		if (!false_nesting)
			sparse_error(token->pos, "expected preprocessor identifier");
		arg = -1;
	}

	return preprocessor_if(stream, token, arg);
}

/*
 * Expression handling for #if and #elif; it differs from normal expansion
 * due to special treatment of "defined".
 */
static int expression_value(struct token **where)
{
	struct expression *expr;
	struct token *p;
	struct token **list = where, **beginning = NULL;
	long long value;
	int state = 0;

	while (!eof_token(p = scan_next(list))) {
		switch (state) {
		case 0:
			if (token_type(p) != TOKEN_IDENT)
				break;
			if (p->ident == &defined_ident) {
				state = 1;
				beginning = list;
				break;
			}
			if (!expand_one_symbol(list))
				continue;
			if (token_type(p) != TOKEN_IDENT)
				break;
			token_type(p) = TOKEN_ZERO_IDENT;
			break;
		case 1:
			if (match_op(p, '(')) {
				state = 2;
			} else {
				state = 0;
				replace_with_defined(p);
				*beginning = p;
			}
			break;
		case 2:
			if (token_type(p) == TOKEN_IDENT)
				state = 3;
			else
				state = 0;
			replace_with_defined(p);
			*beginning = p;
			break;
		case 3:
			state = 0;
			if (!match_op(p, ')'))
				sparse_error(p->pos, "missing ')' after \"defined\"");
			*list = p->next;
			continue;
		}
		list = &p->next;
	}

	p = constant_expression(*where, &expr);
	if (!eof_token(p))
		sparse_error(p->pos, "garbage at end: %s", show_token_sequence(p, 0));
	value = get_expression_value(expr);
	return value != 0;
}

static int handle_if(struct stream *stream, struct token **line, struct token *token)
{
	int value = 0;
	if (!false_nesting)
		value = expression_value(&token->next);

	dirty_stream(stream);
	return preprocessor_if(stream, token, value);
}

static int handle_elif(struct stream * stream, struct token **line, struct token *token)
{
	struct token *top_if = stream->top_if;
	end_group(stream);

	if (!top_if) {
		nesting_error(stream);
		sparse_error(token->pos, "unmatched #elif within stream");
		return 1;
	}

	if (token_type(top_if) == TOKEN_ELSE) {
		nesting_error(stream);
		sparse_error(token->pos, "#elif after #else");
		if (!false_nesting)
			false_nesting = 1;
		return 1;
	}

	dirty_stream(stream);
	if (token_type(top_if) != TOKEN_IF)
		return 1;
	if (false_nesting) {
		false_nesting = 0;
		if (!expression_value(&token->next))
			false_nesting = 1;
	} else {
		false_nesting = 1;
		token_type(top_if) = TOKEN_SKIP_GROUPS;
	}
	return 1;
}

static int handle_else(struct stream *stream, struct token **line, struct token *token)
{
	struct token *top_if = stream->top_if;
	end_group(stream);

	if (!top_if) {
		nesting_error(stream);
		sparse_error(token->pos, "unmatched #else within stream");
		return 1;
	}

	if (token_type(top_if) == TOKEN_ELSE) {
		nesting_error(stream);
		sparse_error(token->pos, "#else after #else");
	}
	if (false_nesting) {
		if (token_type(top_if) == TOKEN_IF)
			false_nesting = 0;
	} else {
		false_nesting = 1;
	}
	token_type(top_if) = TOKEN_ELSE;
	return 1;
}

static int handle_endif(struct stream *stream, struct token **line, struct token *token)
{
	struct token *top_if = stream->top_if;
	end_group(stream);
	if (!top_if) {
		nesting_error(stream);
		sparse_error(token->pos, "unmatched #endif in stream");
		return 1;
	}
	if (false_nesting)
		false_nesting--;
	stream->top_if = top_if->next;
	__free_token(top_if);
	return 1;
}

static int handle_warning(struct stream *stream, struct token **line, struct token *token)
{
	warning(token->pos, "%s", show_token_sequence(token->next, 0));
	return 1;
}

static int handle_error(struct stream *stream, struct token **line, struct token *token)
{
	sparse_error(token->pos, "%s", show_token_sequence(token->next, 0));
	return 1;
}

static int handle_nostdinc(struct stream *stream, struct token **line, struct token *token)
{
	/*
	 * Do we have any non-system includes?
	 * Clear them out if so..
	 */
	*sys_includepath = NULL;
	return 1;
}

static inline void update_inc_ptrs(const char ***where)
{

	if (*where <= dirafter_includepath) {
		dirafter_includepath++;
		/* If this was the entry that we prepend, don't
		 * rise the lower entries, even if they are at
		 * the same level. */
		if (where == &dirafter_includepath)
			return;
	}
	if (*where <= sys_includepath) {
		sys_includepath++;
		if (where == &sys_includepath)
			return;
	}
	if (*where <= isys_includepath) {
		isys_includepath++;
		if (where == &isys_includepath)
			return;
	}

	/* angle_includepath is actually never updated, since we
	 * don't suppport -iquote rught now. May change some day. */
	if (*where <= angle_includepath) {
		angle_includepath++;
		if (where == &angle_includepath)
			return;
	}
}

/* Add a path before 'where' and update the pointers associated with the
 * includepath array */
static void add_path_entry(struct token *token, const char *path,
	const char ***where)
{
	const char **dst;
	const char *next;

	/* Need one free entry.. */
	if (includepath[INCLUDEPATHS-2])
		error_die(token->pos, "too many include path entries");

	/* check that this is not a duplicate */
	dst = includepath;
	while (*dst) {
		if (strcmp(*dst, path) == 0)
			return;
		dst++;
	}
	next = path;
	dst = *where;

	update_inc_ptrs(where);

	/*
	 * Move them all up starting at dst,
	 * insert the new entry..
	 */
	do {
		const char *tmp = *dst;
		*dst = next;
		next = tmp;
		dst++;
	} while (next);
}

static int handle_add_include(struct stream *stream, struct token **line, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token_type(token) != TOKEN_STRING) {
			warning(token->pos, "expected path string");
			return 1;
		}
		add_path_entry(token, token->string->data, &isys_includepath);
	}
}

static int handle_add_isystem(struct stream *stream, struct token **line, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token_type(token) != TOKEN_STRING) {
			sparse_error(token->pos, "expected path string");
			return 1;
		}
		add_path_entry(token, token->string->data, &sys_includepath);
	}
}

static int handle_add_system(struct stream *stream, struct token **line, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token_type(token) != TOKEN_STRING) {
			sparse_error(token->pos, "expected path string");
			return 1;
		}
		add_path_entry(token, token->string->data, &dirafter_includepath);
	}
}

/* Add to end on includepath list - no pointer updates */
static void add_dirafter_entry(struct token *token, const char *path)
{
	const char **dst = includepath;

	/* Need one free entry.. */
	if (includepath[INCLUDEPATHS-2])
		error_die(token->pos, "too many include path entries");

	/* Add to the end */
	while (*dst)
		dst++;
	*dst = path;
	dst++;
	*dst = NULL;
}

static int handle_add_dirafter(struct stream *stream, struct token **line, struct token *token)
{
	for (;;) {
		token = token->next;
		if (eof_token(token))
			return 1;
		if (token_type(token) != TOKEN_STRING) {
			sparse_error(token->pos, "expected path string");
			return 1;
		}
		add_dirafter_entry(token, token->string->data);
	}
}

static int handle_split_include(struct stream *stream, struct token **line, struct token *token)
{
	/*
	 * -I-
	 *  From info gcc:
	 *  Split the include path.  Any directories specified with `-I'
	 *  options before `-I-' are searched only for headers requested with
	 *  `#include "FILE"'; they are not searched for `#include <FILE>'.
	 *  If additional directories are specified with `-I' options after
	 *  the `-I-', those directories are searched for all `#include'
	 *  directives.
	 *  In addition, `-I-' inhibits the use of the directory of the current
	 *  file directory as the first search directory for `#include "FILE"'.
	 */
	quote_includepath = includepath+1;
	angle_includepath = sys_includepath;
	return 1;
}

/*
 * We replace "#pragma xxx" with "__pragma__" in the token
 * stream. Just as an example.
 *
 * We'll just #define that away for now, but the theory here
 * is that we can use this to insert arbitrary token sequences
 * to turn the pragmas into internal front-end sequences for
 * when we actually start caring about them.
 *
 * So eventually this will turn into some kind of extended
 * __attribute__() like thing, except called __pragma__(xxx).
 */
static int handle_pragma(struct stream *stream, struct token **line, struct token *token)
{
	struct token *next = *line;

	if (match_ident(token->next, &once_ident) && eof_token(token->next->next)) {
		stream->once = 1;
		return 1;
	}
	token->ident = &pragma_ident;
	token->pos.newline = 1;
	token->pos.whitespace = 1;
	token->pos.pos = 1;
	*line = token;
	token->next = next;
	return 0;
}

/*
 * We ignore #line for now.
 */
static int handle_line(struct stream *stream, struct token **line, struct token *token)
{
	return 1;
}

static int handle_ident(struct stream *stream, struct token **line, struct token *token)
{
	return 1;
}

static int handle_nondirective(struct stream *stream, struct token **line, struct token *token)
{
	sparse_error(token->pos, "unrecognized preprocessor line '%s'", show_token_sequence(token, 0));
	return 1;
}

static struct token *first_arg(struct arg *args)
{
	struct token *arg = args[1].arg[ARG_QUOTED];
	expand_list(&arg);
	return arg;
}

static bool expand_has_attribute(struct token *token, struct arg *args)
{
	struct token *arg = first_arg(args);
	struct symbol *sym;

	if (token_type(arg) != TOKEN_IDENT) {
		sparse_error(arg->pos, "identifier expected");
		return false;
	}

	sym = lookup_symbol(arg->ident, NS_KEYWORD);
	replace_with_bool(token, sym && sym->op && sym->op->attribute);
	return true;
}

static bool expand_has_builtin(struct token *token, struct arg *args)
{
	struct token *arg = first_arg(args);
	struct symbol *sym;

	if (token_type(arg) != TOKEN_IDENT) {
		sparse_error(arg->pos, "identifier expected");
		return false;
	}

	sym = lookup_symbol(arg->ident, NS_SYMBOL);
	replace_with_bool(token, sym && sym->builtin);
	return true;
}

static bool expand_has_extension(struct token *token, struct arg *args)
{
	struct token *arg = first_arg(args);
	struct ident *ident;
	bool val = false;

	if (token_type(arg) != TOKEN_IDENT) {
		sparse_error(arg->pos, "identifier expected");
		return false;
	}

	ident = arg->ident;
	if (ident == &c_alignas_ident)
		val = true;
	else if (ident == &c_alignof_ident)
		val = true;
	else if (ident == &c_generic_selections_ident)
		val = true;
	else if (ident == &c_static_assert_ident)
		val = true;

	replace_with_bool(token, val);
	return 1;
}

static bool expand_has_feature(struct token *token, struct arg *args)
{
	struct token *arg = first_arg(args);
	struct ident *ident;
	bool val = false;

	if (token_type(arg) != TOKEN_IDENT) {
		sparse_error(arg->pos, "identifier expected");
		return false;
	}

	ident = arg->ident;
	if (standard >= STANDARD_C11) {
		if (ident == &c_alignas_ident)
			val = true;
		else if (ident == &c_alignof_ident)
			val = true;
		else if (ident == &c_generic_selections_ident)
			val = true;
		else if (ident == &c_static_assert_ident)
			val = true;
	}

	replace_with_bool(token, val);
	return 1;
}

static void init_preprocessor(void)
{
	int i;
	int stream = init_stream(NULL, "preprocessor", -1, includepath);
	static struct {
		const char *name;
		int (*handler)(struct stream *, struct token **, struct token *);
	} normal[] = {
		{ "define",		handle_define },
		{ "weak_define",	handle_weak_define },
		{ "strong_define",	handle_strong_define },
		{ "undef",		handle_undef },
		{ "strong_undef",	handle_strong_undef },
		{ "warning",		handle_warning },
		{ "error",		handle_error },
		{ "include",		handle_include },
		{ "include_next",	handle_include_next },
		{ "pragma",		handle_pragma },
		{ "line",		handle_line },
		{ "ident",		handle_ident },

		// our internal preprocessor tokens
		{ "nostdinc",	   handle_nostdinc },
		{ "add_include",   handle_add_include },
		{ "add_isystem",   handle_add_isystem },
		{ "add_system",    handle_add_system },
		{ "add_dirafter",  handle_add_dirafter },
		{ "split_include", handle_split_include },
		{ "argv_include",  handle_argv_include },
	}, special[] = {
		{ "ifdef",	handle_ifdef },
		{ "ifndef",	handle_ifndef },
		{ "else",	handle_else },
		{ "endif",	handle_endif },
		{ "if",		handle_if },
		{ "elif",	handle_elif },
	};
	static struct {
		const char *name;
		void (*expand_simple)(struct token *);
		bool (*expand)(struct token *, struct arg *args);
	} dynamic[] = {
		{ "__LINE__",		expand_line },
		{ "__FILE__",		expand_file },
		{ "__BASE_FILE__",	expand_basefile },
		{ "__DATE__",		expand_date },
		{ "__TIME__",		expand_time },
		{ "__COUNTER__",	expand_counter },
		{ "__INCLUDE_LEVEL__",	expand_include_level },
		{ "__has_attribute",	NULL, expand_has_attribute },
		{ "__has_builtin",	NULL, expand_has_builtin },
		{ "__has_extension",	NULL, expand_has_extension },
		{ "__has_feature",	NULL, expand_has_feature },
	};

	for (i = 0; i < ARRAY_SIZE(normal); i++) {
		struct symbol *sym;
		sym = create_symbol(stream, normal[i].name, SYM_PREPROCESSOR, NS_PREPROCESSOR);
		sym->handler = normal[i].handler;
		sym->normal = 1;
	}
	for (i = 0; i < ARRAY_SIZE(special); i++) {
		struct symbol *sym;
		sym = create_symbol(stream, special[i].name, SYM_PREPROCESSOR, NS_PREPROCESSOR);
		sym->handler = special[i].handler;
		sym->normal = 0;
	}
	for (i = 0; i < ARRAY_SIZE(dynamic); i++) {
		struct symbol *sym;
		sym = create_symbol(stream, dynamic[i].name, SYM_NODE, NS_MACRO);
		sym->expand_simple = dynamic[i].expand_simple;
		if ((sym->expand = dynamic[i].expand) != NULL) {
			sym->fixed_args = 1;
			sym->vararg = false;
			sym->arglist = &eof_token_entry;
		}
	}

	counter_macro = 0;
}

static void handle_preprocessor_line(struct stream *stream, struct token **line, struct token *start)
{
	int (*handler)(struct stream *, struct token **, struct token *);
	struct token *token = start->next;
	int is_normal = 1;
	int is_cond = 0;	// is one of {is,ifdef,ifndef,elif,else,endif}

	if (eof_token(token))
		return;

	if (token_type(token) == TOKEN_IDENT) {
		struct symbol *sym = lookup_symbol(token->ident, NS_PREPROCESSOR);
		if (sym) {
			handler = sym->handler;
			is_normal = sym->normal;
			is_cond = !sym->normal;
		} else {
			handler = handle_nondirective;
		}
	} else if (token_type(token) == TOKEN_NUMBER) {
		handler = handle_line;
	} else {
		handler = handle_nondirective;
	}

	if (is_normal) {
		dirty_stream(stream);
		if (false_nesting)
			goto out;
	}

	if (expanding) {
		if (!is_cond || Wpedantic)
			warning(start->pos, "directive in macro's argument list");
	}
	if (!handler(stream, line, token))	/* all set */
		return;

out:
	free_preprocessor_line(token);
}

static void preprocessor_line(struct stream *stream, struct token **line)
{
	struct token *start = *line, *next;
	struct token **tp = &start->next;

	for (;;) {
		next = *tp;
		if (next->pos.newline)
			break;
		tp = &next->next;
	}
	*line = next;
	*tp = &eof_token_entry;
	handle_preprocessor_line(stream, line, start);
}

static void do_preprocess(struct token **list)
{
	struct token *next;

	while (!eof_token(next = scan_next(list))) {
		struct stream *stream = input_streams + next->pos.stream;

		if (next->pos.newline && match_op(next, '#')) {
			if (!next->pos.noexpand) {
				preprocessor_line(stream, list);
				__free_token(next);	/* Free the '#' token */
				continue;
			}
		}

		switch (token_type(next)) {
		case TOKEN_STREAMEND:
			if (stream->top_if) {
				nesting_error(stream);
				sparse_error(stream->top_if->pos, "unterminated preprocessor conditional");
				stream->top_if = NULL;
				false_nesting = 0;
			}
			if (!stream->dirty)
				stream->constant = CONSTANT_FILE_YES;
			*list = next->next;
			include_level--;
			continue;
		case TOKEN_STREAMBEGIN:
			*list = next->next;
			include_level++;
			continue;

		default:
			dirty_stream(stream);
			if (false_nesting) {
				*list = next->next;
				__free_token(next);
				continue;
			}

			if (token_type(next) != TOKEN_IDENT ||
			    expand_one_symbol(list))
				list = &next->next;
		}
	}
}

void init_include_path(void)
{
	char path[256];
	char os[32];
	int error;
	struct utsname name;

	error = uname(&name);
	if (error)
		return;

	if (strcmp(name.sysname, "Linux") != 0)
		return;
	strcpy(os, "linux-gnu");

	snprintf(path, sizeof(path), "/usr/include/%s-%s/",
			name.machine, os);
	add_pre_buffer("#add_system \"%s/\"\n", path);
}

struct token * preprocess(struct token *token)
{
	preprocessing = 1;
	init_preprocessor();
	do_preprocess(&token);

	// Drop all expressions from preprocessing, they're not used any more.
	// This is not true when we have multiple files, though ;/
	// clear_expression_alloc();
	preprocessing = 0;

	return token;
}

static void dump_body(struct token *token, struct ident *args[])
{
	bool first = true;
	while (!eof_token(token) && token_type(token) != TOKEN_UNTAINT) {
		struct token *next = token->next;
		if (!first && token->pos.whitespace)
			putchar(' ');
		first = false;
		switch (token_type(token)) {
		case TOKEN_CONCAT:
			printf("##");
			break;
		case TOKEN_MACRO_ARGUMENT:
			if (argkind(token) == ARG_STR)
				printf("#");
			printf("%s", show_ident(args[argnum(token)]));
			break;
		default:
			printf("%s", show_token(token));
			break;
		case TOKEN_VA_OPT_STR:
			printf("#");
			/* fall-through */
		case TOKEN_VA_OPT:
			if (is_end_va_opt(token))
				break;
			printf("__VA_OPT__(");
			dump_body(token->va_opt_linkage->next, args);
			printf(")");
		}
		token = next;
	}
}

static void dump_macro(struct symbol *sym)
{
	int fixed_args = sym->fixed_args;
	struct ident *args[fixed_args + 1];
	struct token *token;

	printf("#define %s", show_ident(sym->ident));
	token = sym->arglist;
	if (token) {
		args[0] = &__VA_ARGS___ident;
		for (int n = 1; !eof_token(token); token = token->next) {
			printf("%s", show_token(token));
			if (token_type(token) == TOKEN_IDENT) {
				args[n] = token->ident;
				if (n++ == fixed_args)
					n = 0;
			}
		}
	}
	putchar(' ');

	token = sym->expansion;
	dump_body(token, args);
	putchar('\n');
}

void dump_macro_definitions(void)
{
	struct ident *name;

	FOR_EACH_PTR(macros, name) {
		struct symbol *sym = lookup_macro(name);
		if (sym)
			dump_macro(sym);
	} END_FOR_EACH_PTR(name);
}
