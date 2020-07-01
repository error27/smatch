/*
 * 'sparse' library helper routines.
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
#include <ctype.h>
#include <errno.h>
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
#include "evaluate.h"
#include "scope.h"
#include "linearize.h"
#include "target.h"
#include "machine.h"
#include "version.h"
#include "bits.h"


struct token *skip_to(struct token *token, int op)
{
	while (!match_op(token, op) && !eof_token(token))
		token = token->next;
	return token;
}

static struct token bad_token = { .pos.type = TOKEN_BAD };
struct token *expect(struct token *token, int op, const char *where)
{
	if (!match_op(token, op)) {
		if (token != &bad_token) {
			bad_token.next = token;
			sparse_error(token->pos, "Expected %s %s", show_special(op), where);
			sparse_error(token->pos, "got %s", show_token(token));
		}
		if (op == ';')
			return skip_to(token, op);
		return &bad_token;
	}
	return token->next;
}

///
// issue an error message on new parsing errors
// @token: the current token
// @errmsg: the error message
// If the current token is from a previous error, an error message
// has already been issued, so nothing more is done.
// Otherwise, @errmsg is displayed followed by the current token.
void unexpected(struct token *token, const char *errmsg)
{
	if (token == &bad_token)
		return;
	sparse_error(token->pos, "%s", errmsg);
	sparse_error(token->pos, "got %s", show_token(token));
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

	/* Shut up warnings if position is bad_token.pos */
	if (pos.type == TOKEN_BAD)
		return;

	vsprintf(buffer, fmt, args);	
	name = stream_name(pos.stream);
		
	fflush(stdout);
	fprintf(stderr, "%s:%d:%d: %s%s%s\n",
		name, pos.line, pos.pos, diag_prefix, type, buffer);
}

static int show_info = 1;

void info(struct position pos, const char * fmt, ...)
{
	va_list args;

	if (!show_info)
		return;
	va_start(args, fmt);
	do_warn("", pos, fmt, args);
	va_end(args);
}

static void do_error(struct position pos, const char * fmt, va_list args)
{
	static int errors = 0;
        die_if_error = 1;
	show_info = 1;
	/* Shut up warnings if position is bad_token.pos */
	if (pos.type == TOKEN_BAD)
		return;
	/* Shut up warnings after an error */
	has_error |= ERROR_CURR_PHASE;
	if (errors > 100) {
		static int once = 0;
		show_info = 0;
		if (once)
			return;
		fmt = "too many errors";
		once = 1;
	}

	do_warn("error: ", pos, fmt, args);
	errors++;
}	

void warning(struct position pos, const char * fmt, ...)
{
	va_list args;

	if (Wsparse_error) {
		va_start(args, fmt);
		do_error(pos, fmt, args);
		va_end(args);
		return;
	}

	if (!fmax_warnings || has_error) {
		show_info = 0;
		return;
	}

	if (!--fmax_warnings) {
		show_info = 0;
		fmt = "too many warnings";
	}

	va_start(args, fmt);
	do_warn("warning: ", pos, fmt, args);
	va_end(args);
}

void sparse_error(struct position pos, const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_error(pos, fmt, args);
	va_end(args);
}

void expression_error(struct expression *expr, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_error(expr->pos, fmt, args);
	va_end(args);
	expr->ctype = &bad_ctype;
}

NORETURN_ATTR
void error_die(struct position pos, const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_warn("error: ", pos, fmt, args);
	va_end(args);
	exit(1);
}

NORETURN_ATTR
void die(const char *fmt, ...)
{
	va_list args;
	static char buffer[512];

	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	fprintf(stderr, "%s%s\n", diag_prefix, buffer);
	exit(1);
}

////////////////////////////////////////////////////////////////////////////////

static struct token *pre_buffer_begin = NULL;
static struct token *pre_buffer_end = NULL;

void add_pre_buffer(const char *fmt, ...)
{
	va_list args;
	unsigned int size;
	struct token *begin, *end;
	char buffer[4096];

	va_start(args, fmt);
	size = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	begin = tokenize_buffer(buffer, size, &end);
	if (!pre_buffer_begin)
		pre_buffer_begin = begin;
	if (pre_buffer_end)
		pre_buffer_end->next = begin;
	pre_buffer_end = end;
}

////////////////////////////////////////////////////////////////////////////////
// Predefines

#define	PTYPE_SIZEOF	(1U << 0)
#define	PTYPE_T		(1U << 1)
#define	PTYPE_MAX	(1U << 2)
#define	PTYPE_MIN	(1U << 3)
#define	PTYPE_WIDTH	(1U << 4)
#define	PTYPE_TYPE	(1U << 5)
#define	PTYPE_ALL	(PTYPE_MAX|PTYPE_SIZEOF|PTYPE_WIDTH)
#define	PTYPE_ALL_T	(PTYPE_MAX|PTYPE_SIZEOF|PTYPE_WIDTH|PTYPE_T)

static void predefined_sizeof(const char *name, const char *suffix, unsigned bits)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "__SIZEOF_%s%s__", name, suffix);
	predefine(buf, 1, "%d", bits/8);
}

static void predefined_width(const char *name, unsigned bits)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_WIDTH__", name);
	predefine(buf, 1, "%d", bits);
}

static void predefined_max(const char *name, struct symbol *type)
{
	const char *suffix = builtin_type_suffix(type);
	unsigned bits = type->bit_size - is_signed_type(type);
	unsigned long long max = bits_mask(bits);
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_MAX__", name);
	predefine(buf, 1, "%#llx%s", max, suffix);
}

static void predefined_min(const char *name, struct symbol *type)
{
	const char *suffix = builtin_type_suffix(type);
	char buf[32];

	snprintf(buf, sizeof(buf), "__%s_MIN__", name);

	if (is_signed_type(type))
		predefine(buf, 1, "(-__%s_MAX__ - 1)", name);
	else
		predefine(buf, 1, "0%s", suffix);
}

static void predefined_type(const char *name, struct symbol *type)
{
	const char *typename = builtin_typename(type);
	add_pre_buffer("#weak_define __%s_TYPE__ %s\n", name, typename);
}

static void predefined_ctype(const char *name, struct symbol *type, int flags)
{
	unsigned bits = type->bit_size;

	if (flags & PTYPE_SIZEOF) {
		const char *suffix = (flags & PTYPE_T) ? "_T" : "";
		predefined_sizeof(name, suffix, bits);
	}
	if (flags & PTYPE_MAX)
		predefined_max(name, type);
	if (flags & PTYPE_MIN)
		predefined_min(name, type);
	if (flags & PTYPE_TYPE)
		predefined_type(name, type);
	if (flags & PTYPE_WIDTH)
		predefined_width(name, bits);
}

static void predefined_macros(void)
{
	predefine("__CHECKER__", 0, "1");
	predefine("__GNUC__", 1, "%d", gcc_major);
	predefine("__GNUC_MINOR__", 1, "%d", gcc_minor);
	predefine("__GNUC_PATCHLEVEL__", 1, "%d", gcc_patchlevel);

	predefine("__STDC__", 1, "1");
	predefine("__STDC_HOSTED__", 0, fhosted ? "1" : "0");
	switch (standard) {
	default:
		break;

	case STANDARD_C94:
		predefine("__STDC_VERSION__", 1, "199409L");
		break;

	case STANDARD_C99:
	case STANDARD_GNU99:
		predefine("__STDC_VERSION__", 1, "199901L");
		break;

	case STANDARD_C11:
	case STANDARD_GNU11:
		predefine("__STDC_VERSION__", 1, "201112L");
		break;
	case STANDARD_C17:
	case STANDARD_GNU17:
		predefine("__STDC_VERSION__", 1, "201710L");
		break;
	}
	if (!(standard & STANDARD_GNU) && (standard != STANDARD_NONE))
		predefine("__STRICT_ANSI__", 1, "1");
	if (standard >= STANDARD_C11) {
		predefine("__STDC_NO_ATOMICS__", 1, "1");
		predefine("__STDC_NO_COMPLEX__", 1, "1");
		predefine("__STDC_NO_THREADS__", 1, "1");
	}

	predefine("__CHAR_BIT__", 1, "%d", bits_in_char);
	if (funsigned_char)
		predefine("__CHAR_UNSIGNED__", 1, "1");

	predefined_ctype("SHORT",     &short_ctype, PTYPE_SIZEOF);
	predefined_ctype("SHRT",      &short_ctype, PTYPE_MAX|PTYPE_WIDTH);
	predefined_ctype("SCHAR",     &schar_ctype, PTYPE_MAX|PTYPE_WIDTH);
	predefined_ctype("WCHAR",      wchar_ctype, PTYPE_ALL_T|PTYPE_MIN|PTYPE_TYPE);
	predefined_ctype("WINT",        wint_ctype, PTYPE_ALL_T|PTYPE_MIN|PTYPE_TYPE);
	predefined_ctype("CHAR16",   &ushort_ctype, PTYPE_TYPE);
	predefined_ctype("CHAR32",    uint32_ctype, PTYPE_TYPE);

	predefined_ctype("INT",         &int_ctype, PTYPE_ALL);
	predefined_ctype("LONG",       &long_ctype, PTYPE_ALL);
	predefined_ctype("LONG_LONG", &llong_ctype, PTYPE_ALL);

	predefined_ctype("INT8",      &schar_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT8",     &uchar_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT16",     &short_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT16",   &ushort_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT32",      int32_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT32",    uint32_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INT64",      int64_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("UINT64",    uint64_ctype, PTYPE_MAX|PTYPE_TYPE);

	predefined_ctype("INTMAX",    intmax_ctype, PTYPE_MAX|PTYPE_TYPE|PTYPE_WIDTH);
	predefined_ctype("UINTMAX",  uintmax_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("INTPTR",   ssize_t_ctype, PTYPE_MAX|PTYPE_TYPE|PTYPE_WIDTH);
	predefined_ctype("UINTPTR",   size_t_ctype, PTYPE_MAX|PTYPE_TYPE);
	predefined_ctype("PTRDIFF",  ssize_t_ctype, PTYPE_ALL_T|PTYPE_TYPE);
	predefined_ctype("SIZE",      size_t_ctype, PTYPE_ALL_T|PTYPE_TYPE);
	predefined_ctype("POINTER",     &ptr_ctype, PTYPE_SIZEOF);

	predefined_sizeof("FLOAT", "", bits_in_float);
	predefined_sizeof("DOUBLE", "", bits_in_double);
	predefined_sizeof("LONG_DOUBLE", "", bits_in_longdouble);

	if (arch_target->has_int128)
		predefined_sizeof("INT128", "", 128);

	predefine("__ORDER_LITTLE_ENDIAN__", 1, "1234");
	predefine("__ORDER_BIG_ENDIAN__", 1, "4321");
	predefine("__ORDER_PDP_ENDIAN__", 1, "3412");
	if (arch_big_endian) {
		predefine("__BIG_ENDIAN__", 1, "1");
		predefine("__BYTE_ORDER__", 1, "__ORDER_BIG_ENDIAN__");
	} else {
		predefine("__LITTLE_ENDIAN__", 1, "1");
		predefine("__BYTE_ORDER__", 1, "__ORDER_LITTLE_ENDIAN__");
	}

	if (optimize_level)
		predefine("__OPTIMIZE__", 0, "1");
	if (optimize_size)
		predefine("__OPTIMIZE_SIZE__", 0, "1");

	predefine("__PRAGMA_REDEFINE_EXTNAME", 1, "1");

	// Temporary hacks
	predefine("__extension__", 0, NULL);
	predefine("__pragma__", 0, NULL);

	switch (arch_m64) {
	case ARCH_LP32:
		break;
	case ARCH_X32:
		predefine("__ILP32__", 1, "1");
		predefine("_ILP32", 1, "1");
		break;
	case ARCH_LP64:
		predefine("__LP64__", 1, "1");
		predefine("_LP64", 1, "1");
		break;
	case ARCH_LLP64:
		predefine("__LLP64__", 1, "1");
		break;
	}

	if (fpic) {
		predefine("__pic__", 0, "%d", fpic);
		predefine("__PIC__", 0, "%d", fpic);
	}
	if (fpie) {
		predefine("__pie__", 0, "%d", fpie);
		predefine("__PIE__", 0, "%d", fpie);
	}

	if (arch_target->predefine)
		arch_target->predefine(arch_target);

	if (arch_os >= OS_UNIX) {
		predefine("__unix__", 1, "1");
		predefine("__unix", 1, "1");
		predefine_nostd("unix");
	}

	if (arch_os == OS_SUNOS) {
		predefine("__sun__", 1, "1");
		predefine("__sun", 1, "1");
		predefine_nostd("sun");
		predefine("__svr4__", 1, "1");
	}
}

////////////////////////////////////////////////////////////////////////////////

static void create_builtin_stream(void)
{
	// Temporary hack
	add_pre_buffer("#define _Pragma(x)\n");

	/* add the multiarch include directories, if any */
	if (multiarch_dir && *multiarch_dir) {
		add_pre_buffer("#add_system \"/usr/include/%s\"\n", multiarch_dir);
		add_pre_buffer("#add_system \"/usr/local/include/%s\"\n", multiarch_dir);
	}

	/* We add compiler headers path here because we have to parse
	 * the arguments to get it, falling back to default. */
	add_pre_buffer("#add_system \"%s/include\"\n", gcc_base_dir);
	add_pre_buffer("#add_system \"%s/include-fixed\"\n", gcc_base_dir);

	add_pre_buffer("#define __builtin_stdarg_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_ms_va_start(a,b) ((a) = (__builtin_ms_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_arg(arg,type)  ({ type __va_arg_ret = *(type *)(arg); arg += sizeof(type); __va_arg_ret; })\n");
	add_pre_buffer("#define __builtin_va_alist (*(void *)0)\n");
	add_pre_buffer("#define __builtin_va_arg_incr(x) ((x) + 1)\n");
	add_pre_buffer("#define __builtin_va_copy(dest, src) ({ dest = src; (void)0; })\n");
	add_pre_buffer("#define __builtin_ms_va_copy(dest, src) ({ dest = src; (void)0; })\n");
	add_pre_buffer("#define __builtin_va_end(arg)\n");
	add_pre_buffer("#define __builtin_ms_va_end(arg)\n");
	add_pre_buffer("#define __builtin_va_arg_pack()\n");
}

static struct symbol_list *sparse_tokenstream(struct token *token)
{
	int builtin = token && !token->pos.stream;

	// Preprocess the stream
	token = preprocess(token);

	if (dump_macro_defs || dump_macros_only) {
		if (!builtin)
			dump_macro_definitions();
		if (dump_macros_only)
			return NULL;
	}

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
		token = external_declaration(token, &translation_unit_used_list, NULL);
	return translation_unit_used_list;
}

static struct symbol_list *sparse_file(const char *filename)
{
	int fd;
	struct token *token;

	if (strcmp(filename, "-") == 0) {
		fd = 0;
	} else {
		fd = open(filename, O_RDONLY);
		if (fd < 0)
			die("No such file: %s", filename);
	}
	base_filename = filename;

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
 * affect all subsequent files too, i.e. we can have non-local
 * behaviour between files!
 */
static struct symbol_list *sparse_initial(void)
{
	int i;

	// Prepend any "include" file to the stream.
	// We're in global scope, it will affect all files!
	for (i = 0; i < cmdline_include_nr; i++)
		add_pre_buffer("#argv_include \"%s\"\n", cmdline_include[i]);

	return sparse_tokenstream(pre_buffer_begin);
}

struct symbol_list *sparse_initialize(int argc, char **argv, struct string_list **filelist)
{
	char **args;
	struct symbol_list *list;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	// initialize the default target to the native 'machine'
	target_config(MACH_NATIVE);

	args = argv;
	for (;;) {
		char *arg = *++args;
		if (!arg)
			break;

		if (arg[0] == '-' && arg[1]) {
			args = handle_switch(arg+1, args);
			continue;
		}
		add_ptr_list(filelist, arg);
	}
	handle_switch_finalize();

	// Redirect stdout if needed
	if (dump_macro_defs || preprocess_only)
		do_output = 1;
	if (do_output && outfile && strcmp(outfile, "-")) {
		if (!freopen(outfile, "w", stdout))
			die("error: cannot open %s: %s", outfile, strerror(errno));
	}

	if (fdump_ir == 0)
		fdump_ir = PASS_FINAL;

	list = NULL;
	if (filelist) {
		// Initialize type system
		target_init();
		init_ctype();

		predefined_macros();
		create_builtin_stream();
		init_builtins(0);

		list = sparse_initial();

		/*
		 * Protect the initial token allocations, since
		 * they need to survive all the others
		 */
		protect_token_alloc();
	}
	/*
	 * Evaluate the complete symbol list
	 * Note: This is not needed for normal cases.
	 *	 These symbols should only be predefined defines and
	 *	 declaratons which will be evaluated later, when needed.
	 *	 This is also the case when a file is directly included via
	 *	 '-include <file>' on the command line *AND* the file only
	 *	 contains defines, declarations and inline definitions.
	 *	 However, in the rare cases where the given file should
	 *	 contain some definitions, these will never be evaluated
	 *	 and thus won't be able to be linearized correctly.
	 *	 Hence the evaluate_symbol_list() here under.
	 */
	evaluate_symbol_list(list);
	return list;
}

struct symbol_list * sparse_keep_tokens(char *filename)
{
	struct symbol_list *res;

	/* Clear previous symbol list */
	translation_unit_used_list = NULL;

	new_file_scope();
	res = sparse_file(filename);

	/* And return it */
	return res;
}


struct symbol_list * __sparse(char *filename)
{
	struct symbol_list *res;

	res = sparse_keep_tokens(filename);

	/* Drop the tokens for this file after parsing */
	clear_token_alloc();

	/* And return it */
	return res;
}

struct symbol_list * sparse(char *filename)
{
	struct symbol_list *res = __sparse(filename);

	if (has_error & ERROR_CURR_PHASE)
		has_error = ERROR_PREV_PHASE;
	/* Evaluate the complete symbol list */
	evaluate_symbol_list(res);

	return res;
}
