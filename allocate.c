/*
 * allocate.c - simple space-efficient blob allocator.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
 *
 *  Licensed under the Open Software License version 1.1
 *
 * Simple allocator for data that doesn't get partially free'd.
 * The tokenizer and parser allocate a _lot_ of small data structures
 * (often just two-three bytes for things like small integers),
 * and since they all depend on each other you can't free them
 * individually _anyway_. So do something that is very space-
 * efficient: allocate larger "blobs", and give out individual
 * small bits and pieces of it with no maintenance overhead.
 */
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include "lib.h"
#include "allocate.h"
#include "compat.h"
#include "token.h"
#include "symbol.h"
#include "scope.h"
#include "expression.h"
#include "linearize.h"

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


