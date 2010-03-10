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

int verbose, optimize, optimize_size, preprocessing;
int die_if_error = 0;

#ifndef __GNUC__
# define __GNUC__ 2
# define __GNUC_MINOR__ 95
# define __GNUC_PATCHLEVEL__ 0
#endif

int gcc_major = __GNUC__;
int gcc_minor = __GNUC_MINOR__;
int gcc_patchlevel = __GNUC_PATCHLEVEL__;

static const char *gcc_base_dir = GCC_BASE;

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
			sparse_error(token->pos, "Expected %s %s", show_special(op), where);
			sparse_error(token->pos, "got %s", show_token(token));
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

void warning(struct position pos, const char * fmt, ...)
{
	va_list args;

	if (!max_warnings) {
		show_info = 0;
		return;
	}

	if (!--max_warnings) {
		show_info = 0;
		fmt = "too many warnings";
	}

	va_start(args, fmt);
	do_warn("warning: ", pos, fmt, args);
	va_end(args);
}	

static void do_error(struct position pos, const char * fmt, va_list args)
{
	static int errors = 0;
        die_if_error = 1;
	show_info = 1;
	/* Shut up warnings after an error */
	max_warnings = 0;
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

static struct token *pre_buffer_begin = NULL;
static struct token *pre_buffer_end = NULL;

int Waddress_space = 1;
int Wbitwise = 0;
int Wcast_to_as = 0;
int Wcast_truncate = 1;
int Wcontext = 1;
int Wdecl = 1;
int Wdefault_bitfield_sign = 0;
int Wdesignated_init = 1;
int Wdo_while = 0;
int Wenum_mismatch = 1;
int Wnon_pointer_null = 1;
int Wold_initializer = 1;
int Wone_bit_signed_bitfield = 1;
int Wparen_string = 0;
int Wptr_subtraction_blows = 0;
int Wreturn_void = 0;
int Wshadow = 0;
int Wtransparent_union = 0;
int Wtypesign = 0;
int Wundef = 0;
int Wuninitialized = 1;
int Wdeclarationafterstatement = -1;

int dbg_entry = 0;
int dbg_dead = 0;

int preprocess_only;

static enum { STANDARD_C89,
              STANDARD_C94,
              STANDARD_C99,
              STANDARD_GNU89,
              STANDARD_GNU99, } standard = STANDARD_GNU89;

#define CMDLINE_INCLUDE 20
int cmdline_include_nr = 0;
struct cmdline_include cmdline_include[CMDLINE_INCLUDE];


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

static char **handle_switch_D(char *arg, char **next)
{
	const char *name = arg + 1;
	const char *value = "1";

	if (!*name || isspace(*name))
		die("argument to `-D' is missing");

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
	if (arg[1] == '\0')
		preprocess_only = 1;
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
		/* Fall through */
	default:
		add_pre_buffer("#add_include \"%s/\"\n", path);
	}
	return next;
}

static void add_cmdline_include(char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror(filename);
		return;
	}
	if (cmdline_include_nr >= CMDLINE_INCLUDE)
		die("too many include files for %s\n", filename);
	cmdline_include[cmdline_include_nr].filename = filename;
	cmdline_include[cmdline_include_nr].fd = fd;
	cmdline_include_nr++;
}

static char **handle_switch_i(char *arg, char **next)
{
	if (*next && !strcmp(arg, "include"))
		add_cmdline_include(*++next);
	else if (*next && !strcmp(arg, "imacros"))
		add_cmdline_include(*++next);
	else if (*next && !strcmp(arg, "isystem")) {
		char *path = *++next;
		if (!path)
			die("missing argument for -isystem option");
		add_pre_buffer("#add_isystem \"%s/\"\n", path);
	} else if (*next && !strcmp(arg, "idirafter")) {
		char *path = *++next;
		if (!path)
			die("missing argument for -idirafter option");
		add_pre_buffer("#add_dirafter \"%s/\"\n", path);
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
		size_t_ctype = &ulong_ctype;
		ssize_t_ctype = &long_ctype;
	} else if (!strcmp(arg, "msize-long")) {
		size_t_ctype = &ulong_ctype;
		ssize_t_ctype = &long_ctype;
	}
	return next;
}

static char **handle_switch_o(char *arg, char **next)
{
	if (!strcmp (arg, "o")) {       // "-o foo"
		if (!*++next)
			die("argument to '-o' is missing");
	}
	// else "-ofoo"

	return next;
}

static const struct warning {
	const char *name;
	int *flag;
} warnings[] = {
	{ "address-space", &Waddress_space },
	{ "bitwise", &Wbitwise },
	{ "cast-to-as", &Wcast_to_as },
	{ "cast-truncate", &Wcast_truncate },
	{ "context", &Wcontext },
	{ "decl", &Wdecl },
	{ "default-bitfield-sign", &Wdefault_bitfield_sign },
	{ "designated-init", &Wdesignated_init },
	{ "do-while", &Wdo_while },
	{ "enum-mismatch", &Wenum_mismatch },
	{ "non-pointer-null", &Wnon_pointer_null },
	{ "old-initializer", &Wold_initializer },
	{ "one-bit-signed-bitfield", &Wone_bit_signed_bitfield },
	{ "paren-string", &Wparen_string },
	{ "ptr-subtraction-blows", &Wptr_subtraction_blows },
	{ "return-void", &Wreturn_void },
	{ "shadow", &Wshadow },
	{ "transparent-union", &Wtransparent_union },
	{ "typesign", &Wtypesign },
	{ "undef", &Wundef },
	{ "uninitialized", &Wuninitialized },
	{ "declaration-after-statement", &Wdeclarationafterstatement },
};

enum {
	WARNING_OFF,
	WARNING_ON,
	WARNING_FORCE_OFF
};


static char **handle_onoff_switch(char *arg, char **next, const struct warning warnings[], int n)
{
	int flag = WARNING_ON;
	char *p = arg + 1;
	unsigned i;

	if (!strcmp(p, "sparse-all")) {
		for (i = 0; i < n; i++) {
			if (*warnings[i].flag != WARNING_FORCE_OFF)
				*warnings[i].flag = WARNING_ON;
		}
	}

	// Prefixes "no" and "no-" mean to turn warning off.
	if (p[0] == 'n' && p[1] == 'o') {
		p += 2;
		if (p[0] == '-')
			p++;
		flag = WARNING_FORCE_OFF;
	}

	for (i = 0; i < n; i++) {
		if (!strcmp(p,warnings[i].name)) {
			*warnings[i].flag = flag;
			return next;
		}
	}

	// Unknown.
	return NULL;
}

static char **handle_switch_W(char *arg, char **next)
{
	char ** ret = handle_onoff_switch(arg, next, warnings, sizeof warnings/sizeof warnings[0]);
	if (ret)
		return ret;

	// Unknown.
	return next;
}

static struct warning debugs[] = {
	{ "entry", &dbg_entry},
	{ "dead", &dbg_dead},
};


static char **handle_switch_v(char *arg, char **next)
{
	char ** ret = handle_onoff_switch(arg, next, debugs, sizeof debugs/sizeof debugs[0]);
	if (ret)
		return ret;

	// Unknown.
	do {
		verbose++;
	} while (*++arg == 'v');
	return next;
}


static void handle_onoff_switch_finalize(const struct warning warnings[], int n)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		if (*warnings[i].flag == WARNING_FORCE_OFF)
			*warnings[i].flag = WARNING_OFF;
	}
}

static void handle_switch_W_finalize(void)
{
	handle_onoff_switch_finalize(warnings, sizeof(warnings) / sizeof(warnings[0]));

	/* default Wdeclarationafterstatement based on the C dialect */
	if (-1 == Wdeclarationafterstatement)
	{
		switch (standard)
		{
			case STANDARD_C89:
			case STANDARD_C94:
				Wdeclarationafterstatement = 1;
				break;

			case STANDARD_C99:
			case STANDARD_GNU89:
			case STANDARD_GNU99:
				Wdeclarationafterstatement = 0;
				break;

			default:
				assert (0);
		}

	}
}

static void handle_switch_v_finalize(void)
{
	handle_onoff_switch_finalize(debugs, sizeof(debugs) / sizeof(debugs[0]));
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
	optimize_size = arg[1] == 's';
	return next;
}

static char **handle_switch_ftabstop(char *arg, char **next)
{
	char *end;
	unsigned long val;

	if (*arg == '\0')
		die("error: missing argument to \"-ftabstop=\"");

	/* we silently ignore silly values */
	val = strtoul(arg, &end, 10);
	if (*end == '\0' && 1 <= val && val <= 100)
		tabstop = val;

	return next;
}

static char **handle_switch_f(char *arg, char **next)
{
	int flag = 1;

	arg++;

	if (!strncmp(arg, "tabstop=", 8))
		return handle_switch_ftabstop(arg+8, next);

	/* handle switches w/ arguments above, boolean and only boolean below */

	if (!strncmp(arg, "no-", 3)) {
		flag = 0;
		arg += 3;
	}
	/* handle switch here.. */
	return next;
}

static char **handle_switch_G(char *arg, char **next)
{
	if (!strcmp (arg, "G") && *next)
		return next + 1; // "-G 0"
	else
		return next;     // "-G0" or (bogus) terminal "-G"
}

static char **handle_switch_a(char *arg, char **next)
{
	if (!strcmp (arg, "ansi"))
		standard = STANDARD_C89;

	return next;
}

static char **handle_switch_s(char *arg, char **next)
{
	if (!strncmp (arg, "std=", 4))
	{
		arg += 4;

		if (!strcmp (arg, "c89") ||
		    !strcmp (arg, "iso9899:1990"))
			standard = STANDARD_C89;

		else if (!strcmp (arg, "iso9899:199409"))
			standard = STANDARD_C94;

		else if (!strcmp (arg, "c99") ||
			 !strcmp (arg, "c9x") ||
			 !strcmp (arg, "iso9899:1999") ||
			 !strcmp (arg, "iso9899:199x"))
			standard = STANDARD_C99;

		else if (!strcmp (arg, "gnu89"))
			standard = STANDARD_GNU89;

		else if (!strcmp (arg, "gnu99") || !strcmp (arg, "gnu9x"))
			standard = STANDARD_GNU99;

		else
			die ("Unsupported C dialect");
	}

	return next;
}

static char **handle_nostdinc(char *arg, char **next)
{
	add_pre_buffer("#nostdinc\n");
	return next;
}

static char **handle_base_dir(char *arg, char **next)
{
	gcc_base_dir = *++next;
	if (!gcc_base_dir)
		die("missing argument for -gcc-base-dir option");
	return next;
}

static char **handle_no_lineno(char *arg, char **next)
{
	no_lineno = 1;
	return next;
}

struct switches {
	const char *name;
	char **(*fn)(char *, char **);
};

static char **handle_switch(char *arg, char **next)
{
	static struct switches cmd[] = {
		{ "nostdinc", handle_nostdinc },
		{ "gcc-base-dir", handle_base_dir},
		{ "no-lineno", handle_no_lineno},
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
	case 'f': return handle_switch_f(arg, next);
	case 'G': return handle_switch_G(arg, next);
	case 'a': return handle_switch_a(arg, next);
	case 's': return handle_switch_s(arg, next);
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
	/* Gaah. gcc knows tons of builtin <string.h> functions */
	add_pre_buffer("extern void *__builtin_memcpy(void *, const void *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern void *__builtin_mempcpy(void *, const void *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern void *__builtin_memset(void *, int, __SIZE_TYPE__);\n");
	add_pre_buffer("extern int __builtin_memcmp(const void *, const void *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern char *__builtin_strcat(char *, const char *);\n");
	add_pre_buffer("extern char *__builtin_strncat(char *, const char *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern int __builtin_strcmp(const char *, const char *);\n");
	add_pre_buffer("extern char *__builtin_strchr(const char *, int);\n");
	add_pre_buffer("extern char *__builtin_strcpy(char *, const char *);\n");
	add_pre_buffer("extern char *__builtin_strncpy(char *, const char *, __SIZE_TYPE__);\n");
	add_pre_buffer("extern __SIZE_TYPE__ __builtin_strspn(const char *, const char *);\n");
	add_pre_buffer("extern __SIZE_TYPE__ __builtin_strcspn(const char *, const char *);\n");
	add_pre_buffer("extern char * __builtin_strpbrk(const char *, const char *);\n");
	add_pre_buffer("extern __SIZE_TYPE__ __builtin_strlen(const char *);\n");

	/* And bitwise operations.. */
	add_pre_buffer("extern int __builtin_clz(int);\n");
	add_pre_buffer("extern int __builtin_clzl(long);\n");
	add_pre_buffer("extern int __builtin_clzll(long long);\n");
	add_pre_buffer("extern int __builtin_ctz(int);\n");
	add_pre_buffer("extern int __builtin_ctzl(long);\n");
	add_pre_buffer("extern int __builtin_ctzll(long long);\n");
	add_pre_buffer("extern int __builtin_ffs(int);\n");
	add_pre_buffer("extern int __builtin_ffsl(long);\n");
	add_pre_buffer("extern int __builtin_ffsll(long long);\n");
	add_pre_buffer("extern int __builtin_popcount(unsigned int);\n");
	add_pre_buffer("extern int __builtin_popcountl(unsigned long);\n");
	add_pre_buffer("extern int __builtin_popcountll(unsigned long long);\n");

	/* And some random ones.. */
	add_pre_buffer("extern void *__builtin_return_address(unsigned int);\n");
	add_pre_buffer("extern void *__builtin_extract_return_addr(void *);\n");
	add_pre_buffer("extern void *__builtin_frame_address(unsigned int);\n");
	add_pre_buffer("extern void __builtin_trap(void);\n");
	add_pre_buffer("extern void *__builtin_alloca(__SIZE_TYPE__);\n");
	add_pre_buffer("extern void __builtin_prefetch (const void *, ...);\n");
	add_pre_buffer("extern long __builtin_alpha_extbl(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_extwl(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_insbl(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_inswl(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_insql(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_inslh(long, long);\n");
	add_pre_buffer("extern long __builtin_alpha_cmpbge(long, long);\n");
	add_pre_buffer("extern long __builtin_labs(long);\n");

	/* And some floating point stuff.. */
	add_pre_buffer("extern int __builtin_isgreater(float, float);\n");
	add_pre_buffer("extern int __builtin_isgreaterequal(float, float);\n");
	add_pre_buffer("extern int __builtin_isless(float, float);\n");
	add_pre_buffer("extern int __builtin_islessequal(float, float);\n");
	add_pre_buffer("extern int __builtin_islessgreater(float, float);\n");
	add_pre_buffer("extern int __builtin_isunordered(float, float);\n");

	/* And some __FORTIFY_SOURCE ones.. */
	add_pre_buffer ("extern __SIZE_TYPE__ __builtin_object_size(void *, int);\n");
	add_pre_buffer ("extern void * __builtin___memcpy_chk(void *, const void *, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern void * __builtin___memmove_chk(void *, const void *, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern void * __builtin___mempcpy_chk(void *, const void *, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern void * __builtin___memset_chk(void *, int, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern int __builtin___sprintf_chk(char *, int, __SIZE_TYPE__, const char *, ...);\n");
	add_pre_buffer ("extern int __builtin___snprintf_chk(char *, __SIZE_TYPE__, int , __SIZE_TYPE__, const char *, ...);\n");
	add_pre_buffer ("extern char * __builtin___stpcpy_chk(char *, const char *, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern char * __builtin___strcat_chk(char *, const char *, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern char * __builtin___strcpy_chk(char *, const char *, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern char * __builtin___strncat_chk(char *, const char *, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern char * __builtin___strncpy_chk(char *, const char *, __SIZE_TYPE__, __SIZE_TYPE__);\n");
	add_pre_buffer ("extern int __builtin___vsprintf_chk(char *, int, __SIZE_TYPE__, const char *, __builtin_va_list);\n");
	add_pre_buffer ("extern int __builtin___vsnprintf_chk(char *, __SIZE_TYPE__, int, __SIZE_TYPE__, const char *, __builtin_va_list ap);\n");
}

void create_builtin_stream(void)
{
	add_pre_buffer("#weak_define __GNUC__ %d\n", gcc_major);
	add_pre_buffer("#weak_define __GNUC_MINOR__ %d\n", gcc_minor);
	add_pre_buffer("#weak_define __GNUC_PATCHLEVEL__ %d\n", gcc_patchlevel);

	/* We add compiler headers path here because we have to parse
	 * the arguments to get it, falling back to default. */
	add_pre_buffer("#add_system \"%s/include\"\n", gcc_base_dir);
	add_pre_buffer("#add_system \"%s/include-fixed\"\n", gcc_base_dir);

	add_pre_buffer("#define __extension__\n");
	add_pre_buffer("#define __pragma__\n");

	// gcc defines __SIZE_TYPE__ to be size_t.  For linux/i86 and
	// solaris/sparc that is really "unsigned int" and for linux/x86_64
	// it is "long unsigned int".  In either case we can probably
	// get away with this.  We need the #weak_define as cgcc will define
	// the right __SIZE_TYPE__.
	if (size_t_ctype == &ulong_ctype)
		add_pre_buffer("#weak_define __SIZE_TYPE__ long unsigned int\n");
	else
		add_pre_buffer("#weak_define __SIZE_TYPE__ unsigned int\n");
	add_pre_buffer("#weak_define __STDC__ 1\n");

	switch (standard)
	{
		case STANDARD_C89:
			add_pre_buffer("#weak_define __STRICT_ANSI__\n");
			break;

		case STANDARD_C94:
			add_pre_buffer("#weak_define __STDC_VERSION__ 199409L\n");
			add_pre_buffer("#weak_define __STRICT_ANSI__\n");
			break;

		case STANDARD_C99:
			add_pre_buffer("#weak_define __STDC_VERSION__ 199901L\n");
			add_pre_buffer("#weak_define __STRICT_ANSI__\n");
			break;

		case STANDARD_GNU89:
			break;

		case STANDARD_GNU99:
			add_pre_buffer("#weak_define __STDC_VERSION__ 199901L\n");
			break;

		default:
			assert (0);
	}

	add_pre_buffer("#define __builtin_stdarg_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_ms_va_start(a,b) ((a) = (__builtin_ms_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_arg(arg,type)  ({ type __va_arg_ret = *(type *)(arg); arg += sizeof(type); __va_arg_ret; })\n");
	add_pre_buffer("#define __builtin_va_alist (*(void *)0)\n");
	add_pre_buffer("#define __builtin_va_arg_incr(x) ((x) + 1)\n");
	add_pre_buffer("#define __builtin_va_copy(dest, src) ({ dest = src; (void)0; })\n");
	add_pre_buffer("#define __builtin_va_end(arg)\n");
	add_pre_buffer("#define __builtin_ms_va_end(arg)\n");

	/* FIXME! We need to do these as special magic macros at expansion time! */
	add_pre_buffer("#define __BASE_FILE__ \"base_file.c\"\n");

	if (optimize)
		add_pre_buffer("#define __OPTIMIZE__ 1\n");
	if (optimize_size)
		add_pre_buffer("#define __OPTIMIZE_SIZE__ 1\n");

	/* GCC defines these for limits.h */
	add_pre_buffer("#weak_define __SHRT_MAX__ " STRINGIFY(__SHRT_MAX__) "\n");
	add_pre_buffer("#weak_define __SCHAR_MAX__ " STRINGIFY(__SCHAR_MAX__) "\n");
	add_pre_buffer("#weak_define __INT_MAX__ " STRINGIFY(__INT_MAX__) "\n");
	add_pre_buffer("#weak_define __LONG_MAX__ " STRINGIFY(__LONG_MAX__) "\n");
	add_pre_buffer("#weak_define __LONG_LONG_MAX__ " STRINGIFY(__LONG_LONG_MAX__) "\n");
	add_pre_buffer("#weak_define __WCHAR_MAX__ " STRINGIFY(__WCHAR_MAX__) "\n");
}

static struct symbol_list *sparse_tokenstream(struct token *token)
{
	// Preprocess the stream
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
 * affect all subsequent files too, i.e. we can have non-local
 * behaviour between files!
 */
static struct symbol_list *sparse_initial(void)
{
	struct token *token;
	int i;

	// Prepend any "include" file to the stream.
	// We're in global scope, it will affect all files!
	token = NULL;
	for (i = cmdline_include_nr - 1; i >= 0; i--)
		token = tokenize(cmdline_include[i].filename, cmdline_include[i].fd,
				 token, includepath);

	// Prepend the initial built-in stream
	if (token)
		pre_buffer_end->next = token;
	return sparse_tokenstream(pre_buffer_begin);
}

struct symbol_list *sparse_initialize(int argc, char **argv, struct string_list **filelist)
{
	char **args;
	struct symbol_list *list;

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
		add_ptr_list_notag(filelist, arg);
	}
	handle_switch_W_finalize();
	handle_switch_v_finalize();

	list = NULL;
	if (!ptr_list_empty(filelist)) {
		// Initialize type system
		init_ctype();

		create_builtin_stream();
		add_pre_buffer("#define __CHECKER__ 1\n");
		if (!preprocess_only)
			declare_builtin_functions();

		list = sparse_initial();

		/*
		 * Protect the initial token allocations, since
		 * they need to survive all the others
		 */
		protect_token_alloc();
	}
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

	/* Evaluate the complete symbol list */
	evaluate_symbol_list(res);

	return res;
}
