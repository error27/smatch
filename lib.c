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
#include <sys/stat.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "scope.h"
#include "linearize.h"
#include "target.h"

int verbose, optimize, preprocessing;

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

/*
 * Simple allocator for data that doesn't get partially free'd.
 * The tokenizer and parser allocate a _lot_ of small data structures
 * (often just two-three bytes for things like small integers),
 * and since they all depend on each other you can't free them
 * individually _anyway_. So do something that is very space-
 * efficient: allocate larger "blobs", and give out individual
 * small bits and pieces of it with no maintenance overhead.
 */
struct allocation_blob {
	struct allocation_blob *next;
	unsigned int left, offset;
	unsigned char data[];
};

struct allocator_struct {
	const char *name;
	struct allocation_blob *blobs;
	unsigned int alignment;
	unsigned int chunking;
	void *freelist;
	/* statistics */
	unsigned int allocations, total_bytes, useful_bytes;
};

void drop_all_allocations(struct allocator_struct *desc)
{
	struct allocation_blob *blob = desc->blobs;

	desc->blobs = NULL;
	desc->allocations = 0;
	desc->total_bytes = 0;
	desc->useful_bytes = 0;
	while (blob) {
		struct allocation_blob *next = blob->next;
		blob_free(blob, desc->chunking);
		blob = next;
	}
}

void free_one_entry(struct allocator_struct *desc, void *entry)
{
	void **p = entry;
	*p = desc->freelist;
	desc->freelist = p;
}

void *allocate(struct allocator_struct *desc, unsigned int size)
{
	unsigned long alignment = desc->alignment;
	struct allocation_blob *blob = desc->blobs;
	void *retval;

	/*
	 * NOTE! The freelist only works with things that are
	 *  (a) sufficiently aligned
	 *  (b) use a constant size
	 * Don't try to free allocators that don't follow
	 * these rules.
	 */
	if (desc->freelist) {
		void **p = desc->freelist;
		retval = p;
		desc->freelist = *p;
		do {
			*p = NULL;
			p++;
		} while ((size -= sizeof(void *)) > 0);
		return retval;
	}

	desc->allocations++;
	desc->useful_bytes += size;
	size = (size + alignment - 1) & ~(alignment-1);
	if (!blob || blob->left < size) {
		unsigned int offset, chunking = desc->chunking;
		struct allocation_blob *newblob = blob_alloc(chunking);
		if (!newblob)
			die("out of memory");
		desc->total_bytes += chunking;
		newblob->next = blob;
		blob = newblob;
		desc->blobs = newblob;
		offset = offsetof(struct allocation_blob, data);
		offset = (offset + alignment - 1) & ~(alignment-1);
		blob->left = chunking - offset;
		blob->offset = offset - offsetof(struct allocation_blob, data);
	}
	retval = blob->data + blob->offset;
	blob->offset += size;
	blob->left -= size;
	return retval;
}

static void show_allocations(struct allocator_struct *x)
{
	fprintf(stderr, "%s: %d allocations, %d bytes (%d total bytes, "
			"%6.2f%% usage, %6.2f average size)\n",
		x->name, x->allocations, x->useful_bytes, x->total_bytes,
		100 * (double) x->useful_bytes / x->total_bytes,
		(double) x->useful_bytes / x->allocations);
}

struct allocator_struct ident_allocator = { "identifiers", NULL, __alignof__(struct ident), CHUNK };
struct allocator_struct token_allocator = { "tokens", NULL, __alignof__(struct token), CHUNK };
struct allocator_struct symbol_allocator = { "symbols", NULL, __alignof__(struct symbol), CHUNK };
struct allocator_struct expression_allocator = { "expressions", NULL, __alignof__(struct expression), CHUNK };
struct allocator_struct statement_allocator = { "statements", NULL, __alignof__(struct statement), CHUNK };
struct allocator_struct string_allocator = { "strings", NULL, __alignof__(struct statement), CHUNK };
struct allocator_struct scope_allocator = { "scopes", NULL, __alignof__(struct scope), CHUNK };
struct allocator_struct bytes_allocator = { "bytes", NULL, 1, CHUNK };
struct allocator_struct basic_block_allocator = { "basic_block", NULL, __alignof__(struct basic_block), CHUNK };
struct allocator_struct entrypoint_allocator = { "entrypoint", NULL, __alignof__(struct entrypoint), CHUNK };
struct allocator_struct instruction_allocator = { "instruction", NULL, __alignof__(struct instruction), CHUNK };
struct allocator_struct multijmp_allocator = { "multijmp", NULL, __alignof__(struct multijmp), CHUNK };
struct allocator_struct pseudo_allocator = { "pseudo", NULL, __alignof__(struct pseudo), CHUNK };

#define __ALLOCATOR(type, size, x)				\
	type *__alloc_##x(int extra)				\
	{							\
		return allocate(&x##_allocator, size+extra);	\
	}							\
	void __free_##x(type *entry)				\
	{							\
		return free_one_entry(&x##_allocator, entry);	\
	}							\
	void show_##x##_alloc(void)				\
	{							\
		show_allocations(&x##_allocator);		\
	}							\
	void clear_##x##_alloc(void)				\
	{							\
		drop_all_allocations(&x##_allocator);		\
	}
#define ALLOCATOR(x) __ALLOCATOR(struct x, sizeof(struct x), x)

ALLOCATOR(ident); ALLOCATOR(token); ALLOCATOR(symbol);
ALLOCATOR(expression); ALLOCATOR(statement); ALLOCATOR(string);
ALLOCATOR(scope); __ALLOCATOR(void, 0, bytes);
ALLOCATOR(basic_block); ALLOCATOR(entrypoint);
ALLOCATOR(instruction);
ALLOCATOR(multijmp);
ALLOCATOR(pseudo);

int ptr_list_size(struct ptr_list *head)
{
	int nr = 0;

	if (head) {
		struct ptr_list *list = head;
		do {
			nr += list->nr;
		} while ((list = list->next) != head);
	}
	return nr;
}

/*
 * Linearize the entries of a list up to a total of 'max',
 * and return the nr of entries linearized.
 *
 * The array to linearize into (second argument) should really
 * be "void *x[]", but we want to let people fill in any kind
 * of pointer array, so let's just call it "void *".
 */
int linearize_ptr_list(struct ptr_list *head, void **arr, int max)
{
	int nr = 0;
	if (head && max > 0) {
		struct ptr_list *list = head;

		do {
			int i = list->nr;
			if (i > max) 
				i = max;
			memcpy(arr, list->list, i*sizeof(void *));
			arr += i;
			nr += i;
			max -= i;
			if (!max)
				break;
		} while ((list = list->next) != head);
	}
	return nr;
}

/*
 * When we've walked the list and deleted entries,
 * we may need to re-pack it so that we don't have
 * any empty blocks left (empty blocks upset the
 * walking code
 */
void pack_ptr_list(struct ptr_list **listp)
{
	struct ptr_list *head = *listp;

	if (head) {
		struct ptr_list *entry = head;
		do {
			struct ptr_list *next;
restart:
			next = entry->next;
			if (!entry->nr) {
				struct ptr_list *prev;
				if (next == entry) {
					*listp = NULL;
					return;
				}
				prev = entry->prev;
				prev->next = next;
				next->prev = prev;
				free(entry);
				if (entry == head) {
					*listp = next;
					head = next;
					goto restart;
				}
			}
			entry = next;
		} while (entry != head);
	}
}		

void **__add_ptr_list(struct ptr_list **listp, void *ptr)
{
	struct ptr_list *list = *listp;
	struct ptr_list *last = NULL; /* gcc complains needlessly */
	void **ret;
	int nr;

	if (!list || (nr = (last = list->prev)->nr) >= LIST_NODE_NR) {
		struct ptr_list *newlist = malloc(sizeof(*newlist));
		if (!newlist)
			die("out of memory for symbol/statement lists");
		memset(newlist, 0, sizeof(*newlist));
		if (!list) {
			newlist->next = newlist;
			newlist->prev = newlist;
			*listp = newlist;
		} else {
			newlist->prev = last;
			newlist->next = list;
			list->prev = newlist;
			last->next = newlist;
		}
		last = newlist;
		nr = 0;
	}
	ret = last->list + nr;
	*ret = ptr;
	nr++;
	last->nr = nr;
	return ret;
}

void delete_ptr_list_entry(struct ptr_list **list, void *entry, int count)
{
	void *ptr;

	FOR_EACH_PTR(*list, ptr) {
		if (ptr == entry) {
			DELETE_CURRENT_PTR(ptr);
			if (!--count)
				goto out;
		}
	} END_FOR_EACH_PTR(ptr);
	assert(count <= 0);
out:
	pack_ptr_list(list);
}

void replace_ptr_list_entry(struct ptr_list **list, void *old_ptr, void *new_ptr, int count)
{
	void *ptr;

	FOR_EACH_PTR(*list, ptr) {
		if (ptr==old_ptr) {
			REPLACE_CURRENT_PTR(ptr, new_ptr);
			if (!--count)
				return;
		}
	}END_FOR_EACH_PTR(ptr);
	assert(count <= 0);
}

void * delete_ptr_list_last(struct ptr_list **head)
{
	void *ptr = NULL;
	struct ptr_list *last, *first = *head;

	if (!first)
		return NULL;
	last = first->prev;
	if (last->nr)
		ptr = last->list[--last->nr];
	if (last->nr <=0) {
		first->prev = last->prev;
		last->prev->next = first;
		if (last == first)
			*head = NULL;
		free(last);
	}
	return ptr;
}

void concat_ptr_list(struct ptr_list *a, struct ptr_list **b)
{
	void *entry;
	FOR_EACH_PTR(a, entry) {
		add_ptr_list(b, entry);
	} END_FOR_EACH_PTR(entry);
}

void __free_ptr_list(struct ptr_list **listp)
{
	struct ptr_list *tmp, *list = *listp;

	if (!list)
		return;

	list->prev->next = NULL;
	while (list) {
		tmp = list;
		list = list->next;
		free(tmp);
	}

	*listp = NULL;
}

static void do_warn(const char *type, struct position pos, const char * fmt, va_list args)
{
	static char buffer[512];
	const char *name;

	vsprintf(buffer, fmt, args);	
	name = input_streams[pos.stream].name;
		
	fprintf(stderr, "%s:%d:%d: %s%s\n",
		name, pos.line, pos.pos, type, buffer);
}

void info(struct position pos, const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_warn("", pos, fmt, args);
	va_end(args);
}

void warning(struct position pos, const char * fmt, ...)
{
	static int warnings = 0;
	va_list args;

	if (warnings > 100) {
		static int once = 0;
		if (once)
			return;
		fmt = "too many warnings";
		once = 1;
	}

	va_start(args, fmt);
	do_warn("warning: ", pos, fmt, args);
	va_end(args);
	warnings++;
}	

void error(struct position pos, const char * fmt, ...)
{
	static int errors = 0;
	va_list args;

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

char **handle_switch_D(char *arg, char **next)
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

char **handle_switch_E(char *arg, char **next)
{
	preprocess_only = 1;
	return next;
}

char **handle_switch_v(char *arg, char **next)
{
	verbose = 1;
	return next;
}

char **handle_switch_I(char *arg, char **next)
{
	char *path = arg+1;

	switch (arg[1]) {
	case '-':
		/* Explaining '-I-' with a google search:
		 *
		 *	"Specifying -I after -I- searches for #include directories.
		 *	 If -I- is specified before, it searches for #include "file"
		 *	 and not #include ."
		 *
		 * Which didn't explain it at all to me. Maybe somebody else can
		 * explain it properly. We ignore it for now.
		 */
		return next;

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

char **handle_switch_i(char *arg, char **next)
{
	if (*next && !strcmp(arg, "include")) {
		char *name = *++next;
		int fd = open(name, O_RDONLY);

		include_fd = fd;
		include = name;
		if (fd < 0)
			perror(name);
	}
	return next;
}

char **handle_switch_M(char *arg, char **next)
{
	if (!strcmp(arg, "MF") || !strcmp(arg,"MQ") || !strcmp(arg,"MT")) {
		if (!*next)
			die("missing argument for -%s option", arg);
		return next + 1;
	}
	return next;
}

char **handle_switch_m(char *arg, char **next)
{
	if (!strcmp(arg, "m64")) {
		bits_in_long = 64;
		max_int_alignment = 8;
		bits_in_pointer = 64;
		pointer_alignment = 8;
	}
	return next;
}

char **handle_switch_o(char *arg, char **next)
{
	if (!strcmp (arg, "o") && *next)
		return next + 1; // "-o foo"
	else
		return next;     // "-ofoo" or (bogus) terminal "-o"
}

const struct warning {
	const char *name;
	int *flag;
} warnings[] = {
	{ "default-bitfield-sign", &Wdefault_bitfield_sign },
	{ "undef", &Wundefined_preprocessor },
	{ "bitwise", &Wbitwise },
	{ "typesign", &Wtypesign },
	{ "context", &Wcontext },
};


char **handle_switch_W(char *arg, char **next)
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

char **handle_switch_U(char *arg, char **next)
{
	const char *name = arg + 1;
	add_pre_buffer ("#undef %s\n", name);
	return next;
}

char **handle_switch_O(char *arg, char **next)
{
	int level = 1;
	if (arg[1] >= '0' && arg[1] <= '9')
		level = arg[1] - '0';
	optimize = level;
	return next;
}

char **handle_nostdinc(char *arg, char **next)
{
	add_pre_buffer("#nostdinc\n");
	return next;
}

struct switches {
	const char *name;
	char **(*fn)(char *, char**);
};

char **handle_switch(char *arg, char **next)
{
	char **rc = next;
	static struct switches cmd[] = {
		{ "nostdinc", handle_nostdinc },
		{ NULL, NULL }
	};
	struct switches *s;

	switch (*arg) {
	case 'D': rc = handle_switch_D(arg, next); break;
	case 'E': rc = handle_switch_E(arg, next); break;
	case 'I': rc = handle_switch_I(arg, next); break;
	case 'i': rc = handle_switch_i(arg, next); break;
	case 'M': rc = handle_switch_M(arg, next); break;
	case 'm': rc = handle_switch_m(arg, next); break;
	case 'o': rc = handle_switch_o(arg, next); break;
	case 'U': rc = handle_switch_U(arg, next); break;
	case 'v': rc = handle_switch_v(arg, next); break;
	case 'W': rc = handle_switch_W(arg, next); break;
	case 'O': rc = handle_switch_O(arg, next); break;
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
	return rc;
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
	add_pre_buffer("#define __GNUC__ 2\n");
	add_pre_buffer("#define __GNUC_MINOR__ 95\n");
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
}

static void do_predefined(char *filename)
{
	add_pre_buffer("#define __BASE_FILE__ \"%s\"\n", filename);
	add_pre_buffer("#define __DATE__ \"??? ?? ????\"\n");
	add_pre_buffer("#define __TIME__ \"??:??:??\"\n");
}

struct symbol_list *sparse(int argc, char **argv)
{
	int fd;
	char *filename = NULL, **args;
	struct token *token;

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
		filename = arg;
	}

	if (!filename)
		die("no input files given");

	// Initialize type system
	init_ctype();

	create_builtin_stream();
	add_pre_buffer("#define __CHECKER__ 1\n");
	if (!preprocess_only)
		declare_builtin_functions();

	do_predefined(filename);

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

	// Prepend any "include" file to the stream.
	if (include_fd >= 0)
		token = tokenize(include, include_fd, token, includepath);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

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
	return translation_unit(token);
}
