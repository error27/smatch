/*
 * 'sparse' library helper routines.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"
#include "scope.h"
#include "linearize.h"

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
			warn(token->pos, "Expected %s %s", show_special(op), where);
			warn(token->pos, "got %s", show_token(token));
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

#define CHUNK 32768
#define blob_alloc(size) mmap(NULL, ((size)+4095) & ~4095, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define blob_free(addr,size) munmap((addr), ((size)+4095) & ~4095)

struct allocator_struct {
	const char *name;
	struct allocation_blob *blobs;
	unsigned int alignment;
	unsigned int chunking;
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

void *allocate(struct allocator_struct *desc, unsigned int size)
{
	unsigned long alignment = desc->alignment;
	struct allocation_blob *blob = desc->blobs;
	void *retval;

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
		if (alignment > offset)
			offset = alignment;
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

#define __ALLOCATOR(type, size, x)				\
	type *__alloc_##x(int extra)				\
	{							\
		return allocate(&x##_allocator, size+extra);	\
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

void iterate(struct ptr_list *head, void (*callback)(void *, void *, int), void *data)
{
	struct ptr_list *list = head;
	int flag = ITERATE_FIRST;

	if (!head)
		return;
	do {
		int i;
		for (i = 0; i < list->nr; i++) {
			if (i == list->nr-1 && list->next == head)
				flag |= ITERATE_LAST;
			callback(list->list[i], data, flag);
			flag = 0;
		}
		list = list->next;
	} while (list != head);
}

void add_ptr_list(struct ptr_list **listp, void *ptr)
{
	struct ptr_list *list = *listp;
	struct ptr_list *last;
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
	last->list[nr] = ptr;
	nr++;
	last->nr = nr;
}

void concat_ptr_list(struct ptr_list *a, struct ptr_list **b)
{
	void *entry;
	FOR_EACH_PTR(a, entry) {
		add_ptr_list(b, entry);
	} END_FOR_EACH_PTR;
}

void free_ptr_list(struct ptr_list **listp)
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
		
	fprintf(stderr, "%s: %s:%d:%d: %s\n",
		type, name, pos.line, pos.pos, buffer);
}

void warn(struct position pos, const char * fmt, ...)
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
	do_warn("warning", pos, fmt, args);
	va_end(args);
	warnings++;
}	

void error(struct position pos, const char * fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	do_warn("error", pos, fmt, args);
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
	const char *value = "";
	for (;;) {
		char c;
		c = *++arg;
		if (!c)
			break;
		if (isspace(c) || c == '=') {
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
	add_pre_buffer("#add_include \"%s/\"\n", arg + 1);
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

char **handle_switch(char *arg, char **next)
{
	char **rc = next;

	switch (*arg) {
	case 'D': rc = handle_switch_D(arg, next); break;
	case 'E': rc = handle_switch_E(arg, next); break;
	case 'v': rc = handle_switch_v(arg, next); break;
	case 'I': rc = handle_switch_I(arg, next); break;
	case 'i': rc = handle_switch_i(arg, next); break;
	default:
		/*
		 * Ignore unknown command line options:
		 * they're probably gcc switches
		 */
		break;
	}
	return rc;
}

void create_builtin_stream(void)
{
	add_pre_buffer("#define __i386__ 1\n");
	add_pre_buffer("#define __linux__ 1\n");
	add_pre_buffer("#define __STDC__ 1\n");
	add_pre_buffer("#define linux linux\n");
	add_pre_buffer("#define cond_syscall(x)\n");
	add_pre_buffer("#define __GNUC__ 2\n");
	add_pre_buffer("#define __GNUC_MINOR__ 95\n");
	add_pre_buffer("#define __func__ \"function\"\n");
	add_pre_buffer("#define __extension__\n");
	add_pre_buffer("#define __pragma__\n");
	add_pre_buffer("#define __builtin_stdarg_start(a,b) ((a) = (__builtin_va_list)(&(b)))\n");
	add_pre_buffer("#define __builtin_va_arg(arg,type)  ((type)0)\n");
	add_pre_buffer("#define __builtin_va_end(arg)\n");	
}
