/*
 * 'sparse' library helper routines.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <sys/types.h>

#include "lib.h"
#include "allocate.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "scope.h"
#include "linearize.h"
#include "target.h"

int verbose, optimize, preprocessing;

#ifndef __GNUC__
# define __GNUC__ 2
# define __GNUC_MINOR__ 95
#endif

int gcc_major = __GNUC__;
int gcc_minor = __GNUC_MINOR__;

struct token *skip_to(struct token *token, int op)
{
	while (!match_op(token, op) && !eof_token(token))
		token = token->next;
	return token;
}

struct token *expect(struct token *token, int op, const char *where)
{
	if (!match_op(token, op)) {
		static struct token bad_token;
		if (token != &bad_token) {
			bad_token.next = token;
			warning(token->pos, "Expected %s %s", show_special(op), where);
			warning(token->pos, "got %s", show_token(token));
		}
		if (op == ';')
			return skip_to(token, op);
		return &bad_token;
	}
	return token->next;
}

unsigned int hexval(unsigned int c)
{
	int retval = 256;
	switch (c) {
	case '0'...'9':
		retval = c - '0';
		break;
	case 'a'...'f':
		retval = c - 'a' + 10;
		break;
	case 'A'...'F':
		retval = c - 'A' + 10;
		break;
	}
	return retval;
}

static void do_warn(const char *type, struct position pos, const char * fmt, va_list args)
{
	static char buffer[512];
	const char *name;

	vsprintf(buffer, fmt, args);	
	name = stream_name(pos.stream);
		
	fprintf(stderr, "%s:%d:%d: %s%s\n",
		name, pos.line, pos.pos, type, buffer);
}

static int max_warnings = 100;

void info(struct position pos, const char * fmt, ...)
{
	va_list args;

	if (!max_warnings)
		return;
	va_start(args, fmt);
	do_warn("", pos, fmt, args);
	va_end(args);
}

void warning(struct position pos, const char * fmt, ...)
{
	va_list args;

	if (!max_warnings)
		return;

	if (!--max_warnings)
		fmt = "too many warnings";

	va_start(args, fmt);
	do_warn("warning: ", pos, fmt, args);
	va_end(args);
}	

void error(struct position pos, const char * fmt, ...)
{
	static int errors = 0;
	va_list args;

	/* Shut up warnings after an error */
	max_warnings = 0;
	if (errors > 100) {
		static int once = 0;
		if (once)
			return;
		fmt = "too many errors";
		once = 1;
	}

	va_start(args, fmt);
	do_warn("error: ", pos, fmt, args);
	va_end(args);
	errors++;
}	

void error_die(struct position pos, const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_warn("error: ", pos, fmt, args);
	va_end(args);
	exit(1);
}

void die(const char *fmt, ...)
{
	va_list args;
	static char buffer[512];

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	fprintf(stderr, "%s\n", buffer);
	exit(1);
}

unsigned int pre_buffer_size;
unsigned char pre_buffer[8192];

int Wdefault_bitfield_sign = 0;
int Wbitwise = 0;
int Wtypesign = 0;
int Wcontext = 0;
int Wundefined_preprocessor = 0;
int Wptr_subtraction_blows = 0;
int Wtransparent_union = 1;
int preprocess_only;
char *include;
int include_fd = -1;

void add_pre_buffer(const char *fmt, ...)
{
	va_list args;
	unsigned int size;

	va_start(args, fmt);
	size = pre_buffer_size;
	size += vsnprintf(pre_buffer + size,
		sizeof(pre_buffer) - size,
		fmt, args);
	pre_buffer_size = size;
	va_end(args);
}

static char **handle_switch_D(char *arg, char **next)
{
	const char *name = arg + 1;
	const char *value = "1";
	for (;;) {
		char c;
		c = *++arg;
		if (!c)
			break;
		if (isspace((unsigned char)c) || c == '=') {
			*arg = '\0';
			value = arg + 1;
			break;
		}
	}
	add_pre_buffer("#define %s %s\n", name, value);
	return next;
}

static char **handle_switch_E(char *arg, char **next)
{
	preprocess_only = 1;
	return next;
}

static char **handle_switch_v(char *arg, char **next)
{
	do {
		verbose++;
	} while (*++arg == 'v');
	return next;
}

static char **handle_switch_I(char *arg, char **next)
{
	char *path = arg+1;

	switch (arg[1]) {
	case '-':
		add_pre_buffer("#split_include\n");
		break;

	case '\0':	/* Plain "-I" */
		path = *++next;
		if (!path)
			die("missing argument for -I option");
		/* Fallthrough */
	default:
		add_pre_buffer("#add_include \"%s/\"\n", path);
	}
	return next;
}

static char **handle_switch_i(char *arg, char **next)
{
	if (*next && !strcmp(arg, "include")) {
		char *name = *++next;
		int fd = open(name, O_RDONLY);

		include_fd = fd;
		include = name;
		if (fd < 0)
			perror(name);
	}
	else if (*next && !strcmp(arg, "isystem")) {
		char *path = *++next;
		if (!path)
			die("missing argument for -isystem option");
		add_pre_buffer("#add_isystem \"%s/\"\n", path);
	}
	return next;
}

static char **handle_switch_M(char *arg, char **next)
{
	if (!strcmp(arg, "MF") || !strcmp(arg,"MQ") || !strcmp(arg,"MT")) {
		if (!*next)
			die("missing argument for -%s option", arg);
		return next + 1;
	}
	return next;
}

static char **handle_switch_m(char *arg, char **next)
{
	if (!strcmp(arg, "m64")) {
		bits_in_long = 64;
		max_int_alignment = 8;
		bits_in_pointer = 64;
		pointer_alignment = 8;
	}
	return next;
}

static char **handle_switch_o(char *arg, char **next)
{
	if (!strcmp (arg, "o") && *next)
		return next + 1; // "-o foo"
	else
		return next;     // "-ofoo" or (bogus) terminal "-o"
}

static const struct warning {
	const char *name;
	int *flag;
} warnings[] = {
	{ "ptr-subtraction-blows", &Wptr_subtraction_blows },
	{ "default-bitfield-sign", &Wdefault_bitfield_sign },
	{ "undef", &Wundefined_preprocessor },
	{ "bitwise", &Wbitwise },
	{ "typesign", &Wtypesign },
	{ "context", &Wcontext },
	{ "transparent-union", &Wtransparent_union },
};


static char **handle_switch_W(char *arg, char **next)
{
	int no = 0;
	char *p = arg + 1;
	unsigned i;

	// Prefixes "no" and "no-" mean to turn warning off.
	if (p[0] == 'n' && p[1] == 'o') {
		p += 2;
		if (p[0] == '-')
			p++;
		no = 1;
	}

	for (i = 0; i < sizeof(warnings) / sizeof(warnings[0]); i++) {
		if (!strcmp(p,warnings[i].name)) {
			*warnings[i].flag = !no;
			return next;
		}
	}

	// Unknown.
	return next;
}

static char **handle_switch_U(char *arg, char **next)
{
	const char *name = arg + 1;
	add_pre_buffer ("#undef %s\n", name);
	return next;
}

static char **handle_switch_O(char *arg, char **next)
{
	int level = 1;
	if (arg[1] >= '0' && arg[1] <= '9')
		level = arg[1] - '0';
	optimize = level;
	return next;
}

static char **handle_nostdinc(char *arg, char **next)
{
	add_pre_buffer("#nostdinc\n");
	return next;
}

static char **handle_dirafter(char *arg, char **next)
{
	char *path = *++next;
	if (!path)
		die("missing argument for -dirafter option");
	add_pre_buffer("#add_dirafter \"%s/\"\n", path);
	return next;
}

struct switches {
	const char *name;
	char **(*fn)(char *, char**);
};

char **handle_switch(char *arg, char **next)
{
	static struct switches cmd[] = {
		{ "nostdinc", handle_nostdinc },
		{ "dirafter", handle_dirafter },
		{ NULL, NULL }
	};
	struct switches *s;

	switch (*arg) {
	case 'D': return handle_switch_D(arg, next);
	case 'E': return handle_switch_E(arg, next);
	case 'I': return handle_switch_I(arg, next);
	case 'i': return handle_switch_i(arg, next);
	case 'M': return handle_switch_M(arg, next);
	case 'm': return handle_switch_m(arg, next);
	case 'o': return handle_switch_o(arg, next);
	case 'U': return handle_switch_U(arg, next);
	case 'v': return handle_switch_v(arg, next);
	case 'W': return handle_switch_W(arg, next);
	case 'O': return handle_switch_O(arg, next);
	default:
		break;
	}

	s = cmd;
	while (s->name) {
		if (!strcmp(s->name, arg))
			return s->fn(arg, next);
		s++;
	}

	/*
	 * Ignore unknown command line options:
	 * they're probably gcc switches
	 */
	return next;
}

void declare_builtin_functions(void)
{
	add_pre_buffer("extern void *__builtin_memcpy(void *, const void *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern void *__builtin_return_address(unsigned int);\n");
	add_pre_buffer("extern void *__builtin_frame_address(unsigned int);\n");
	add_pre_buffer("extern void *__builtin_memset(void *, int, __SIZE_TYPE__);\n");	
	add_pre_buffer("extern void __builtin_trap(void);\n");
	add_pre_buffer("extern int __builtin_ffs(int);\n");
	add_pre_buffer("extern void *__builtin_alloca(__SIZE_TYPE__);\n");
}

void create_builtin_stream(void)
{
	add_pre_buffer("#define __GNUC__ %d\n", gcc_major);
	add_pre_buffer("#define __GNUC_MINOR__ %d\n", gcc_minor);
	add_pre_buffer("#define __extension__\n");
	add_pre_buffer("#define __pragma__\n");

	// gcc defines __SIZE_TYPE__ to be size_t.  For linux/i86 and
	// solaris/sparc that is really "unsigned int" and for linux/x86_64
	// it is "long unsigned int".  In either case we can probably
	// get away with this.  We need the #ifndef as cgcc will define
	// the right __SIZE_TYPE__.
	add_pre_buffer("#weak_define __SIZE_TYPE__ long unsigned int\n");
	add_pre_buffer("#weak_define __STDC__ 1\n");

	add_pre_buffer("#define __builtin_stdarg_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_arg(arg,type)  ({ type __va_arg_ret = *(type *)(arg); arg += sizeof(type); __va_arg_ret; })\n");
	add_pre_buffer("#define __builtin_va_alist (*(void *)0)\n");
	add_pre_buffer("#define __builtin_va_arg_incr(x) ((x) + 1)\n");
	add_pre_buffer("#define __builtin_va_end(arg)\n");
	add_pre_buffer("#define __builtin_offsetof(type, name) ((__SIZE_TYPE__)&((type *)(0ul))->name)\n");

	/* FIXME! We need to do these as special magic macros at expansion time! */
	add_pre_buffer("#define __BASE_FILE__ \"base_file.c\"\n");
	add_pre_buffer("#define __DATE__ \"??? ?? ????\"\n");
	add_pre_buffer("#define __TIME__ \"??:??:??\"\n");
}

static struct symbol_list *sparse_tokenstream(struct token *token)
{
	// Pre-process the stream
	token = preprocess(token);

	if (preprocess_only) {
		while (!eof_token(token)) {
			int prec = 1;
			struct token *next = token->next;
			const char *separator = "";
			if (next->pos.whitespace)
				separator = " ";
			if (next->pos.newline) {
				separator = "\n\t\t\t\t\t";
				prec = next->pos.pos;
				if (prec > 4)
					prec = 4;
			}
			printf("%s%.*s", show_token(token), prec, separator);
			token = next;
		}
		putchar('\n');

		return NULL;
	} 

	// Parse the resulting C code
	while (!eof_token(token))
		token = external_declaration(token, &translation_unit_used_list);
	return translation_unit_used_list;
}

static struct symbol_list *sparse_file(const char *filename)
{
	int fd;
	struct token *token;

	if (strcmp (filename, "-") == 0) {
		fd = 0;
	} else {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			die("No such file: %s", filename);
	}

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL, includepath);
	close(fd);

	return sparse_tokenstream(token);
}

/*
 * This handles the "-include" directive etc: we're in global
 * scope, and all types/macros etc will affect all the following
 * files.
 *
 * NOTE NOTE NOTE! "#undef" of anything in this stage will
 * affect all subsequent files too, ie we can have non-local
 * behaviour between files!
 */
static void sparse_initial(void)
{
	struct token *token;

	// Prepend any "include" file to the stream.
	// We're in global scope, it will affect all files!
	token = NULL;
	if (include_fd >= 0)
		token = tokenize(include, include_fd, NULL, includepath);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);
	sparse_tokenstream(token);
}

struct symbol_list *sparse(int argc, char **argv)
{
	int i;
	struct symbol_list *res;
	char **args;
	int files = 0;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	args = argv;
	for (;;) {
		char *arg = *++args;
		if (!arg)
			break;

		if (arg[0] == '-' && arg[1]) {
			args = handle_switch(arg+1, args);
			continue;
		}

		/*
		 * Hacky hacky hacky: we re-use the argument space
		 * to save the filenames.
		 */
		argv[files++] = arg;
	}

	if (!files)
		die("no input files given");

	// Initialize type system
	init_ctype();

	create_builtin_stream();
	add_pre_buffer("#define __CHECKER__ 1\n");
	if (!preprocess_only)
		declare_builtin_functions();

	sparse_initial();

	/*
	 * Protect the initial token allocations, since
	 * they need to survive all the others
	 */
	protect_token_alloc();

	res = NULL;
	for (i = 0; i < files ; i++) {
		char *filename = argv[i];

		start_file_scope();
		res = sparse_file(filename);
		end_file_scope();

		/* Drop the tokens for this file now */
		clear_token_alloc();
	}

	/* Evaluate the complete symbol list */
	evaluate_symbol_list(res);

	return res;
}
