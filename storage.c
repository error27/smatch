/*
 * Storage - associate pseudos with "storage" that keeps them alive
 * between basic blocks.  The aim is to be able to turn as much of
 * the global storage allocation problem as possible into a local
 * per-basic-block one.
 *
 * Copyright (C) 2004 Linus Torvalds
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "symbol.h"
#include "expression.h"
#include "linearize.h"
#include "storage.h"

ALLOCATOR(storage, "storages");
ALLOCATOR(storage_hash, "storage hash");

static inline struct storage *alloc_storage(void)
{
	return __alloc_storage(0);
}

static inline struct storage_hash *alloc_storage_hash(struct storage *s)
{
	struct storage_hash *entry = __alloc_storage_hash(0);
	struct storage **usep = &entry->storage;

	*usep = s;
	add_ptr_list(&s->users, usep);
	return entry;
}

#define MAX_STORAGE_HASH 64
struct storage_hash_list *storage_hash_table[MAX_STORAGE_HASH];

static inline unsigned int storage_hash(struct basic_block *bb, pseudo_t pseudo, enum inout_enum inout)
{
	unsigned hash = hashval(bb) + hashval(pseudo) + hashval(inout);
	hash += hash / MAX_STORAGE_HASH;
	return hash & (MAX_STORAGE_HASH-1);
}

static int hash_list_cmp(const void *_a, const void *_b)
{
	const struct storage_hash *a = _a;
	const struct storage_hash *b = _b;
	if (a->pseudo != b->pseudo)
		return a->pseudo < b->pseudo ? -1 : 1;
	return 0;
}

static void sort_hash_list(struct storage_hash_list **listp)
{
	sort_list((struct ptr_list **)listp, hash_list_cmp);
}

struct storage_hash_list *gather_storage(struct basic_block *bb, enum inout_enum inout)
{
	int i;
	struct storage_hash *entry, *prev;
	struct storage_hash_list *list = NULL;

	for (i = 0; i < MAX_STORAGE_HASH; i++) {
		struct storage_hash *hash;
		FOR_EACH_PTR(storage_hash_table[i], hash) {
			if (hash->bb == bb && hash->inout == inout)
				add_ptr_list(&list, hash);
		} END_FOR_EACH_PTR(hash);
	}
	sort_hash_list(&list);

	prev = NULL;
	FOR_EACH_PTR(list, entry) {
		if (prev && entry->pseudo == prev->pseudo) {
			assert(entry == prev);
			DELETE_CURRENT_PTR(entry);
		}
		prev = entry;
	} END_FOR_EACH_PTR(entry);
	PACK_PTR_LIST(&list);
	return list;
}

static struct storage *lookup_storage(struct basic_block *bb, pseudo_t pseudo, enum inout_enum inout)
{
	struct storage_hash_list *list = storage_hash_table[storage_hash(bb,pseudo,inout)];
	struct storage_hash *hash;

	FOR_EACH_PTR(list, hash) {
		if (hash->bb == bb && hash->pseudo == pseudo && hash->inout == inout)
			return hash->storage;
	} END_FOR_EACH_PTR(hash);
	return NULL;
}

static void add_storage(struct storage *storage, struct basic_block *bb, pseudo_t pseudo, enum inout_enum inout)
{
	struct storage_hash_list **listp = storage_hash_table + storage_hash(bb,pseudo,inout);
	struct storage_hash *hash = alloc_storage_hash(storage);

	hash->bb = bb;
	hash->pseudo = pseudo;
	hash->inout = inout;

	add_ptr_list(listp, hash);
}

void free_storage(void)
 {
	int i;

	for (i = 0; i < MAX_STORAGE_HASH; i++)
		free_ptr_list(storage_hash_table + i);
}

const char *show_storage(struct storage *s)
{
	static char buffer[1024];
	if (!s)
		return "none";
	switch (s->type) {
	case REG_REG:
		sprintf(buffer, "reg%d", s->regno);
		break;
	case REG_STACK:
		sprintf(buffer, "%d(SP)", s->offset);
		break;
	case REG_ARG:
		sprintf(buffer, "ARG%d", s->regno);
		break;
	default:
		sprintf(buffer, "%d:%d", s->type, s->regno);
		break;
	}
	return buffer;
}

/*
 * Combine two storage allocations into one.
 *
 * We just randomly pick one over the other, and replace
 * the other uses.
 */
static void combine_storage(struct storage *src, struct storage *dst)
{
	struct storage **usep;

	/* Remove uses of "src_storage", replace with "dst" */
	FOR_EACH_PTR(src->users, usep) {
		assert(*usep == src);
		*usep = dst;
		add_ptr_list(&dst->users, usep);
	} END_FOR_EACH_PTR(usep);

	/* Mark it unused */
	src->type = REG_BAD;
	src->users = NULL;
}

static void set_up_bb_storage(struct basic_block *bb)
{
	struct basic_block *child;

	FOR_EACH_PTR(bb->children, child) {
		pseudo_t pseudo;
		FOR_EACH_PTR(child->needs, pseudo) {
			struct storage *child_in, *parent_out;

			parent_out = lookup_storage(bb, pseudo, STOR_OUT);
			child_in = lookup_storage(child, pseudo, STOR_IN);

			if (parent_out) {
				if (!child_in) {
					add_storage(parent_out, child, pseudo, STOR_IN);
					continue;
				}
				if (parent_out == child_in)
					continue;
				combine_storage(parent_out, child_in);
				continue;
			}
			if (child_in) {
				add_storage(child_in, bb, pseudo, STOR_OUT);
				continue;
			}
			parent_out = alloc_storage();
			add_storage(parent_out, bb, pseudo, STOR_OUT);
			add_storage(parent_out, child, pseudo, STOR_IN);
		} END_FOR_EACH_PTR(pseudo);
	} END_FOR_EACH_PTR(child);
}

static void set_up_argument_storage(struct entrypoint *ep, struct basic_block *bb)
{
	pseudo_t arg;

	FOR_EACH_PTR(bb->needs, arg) {
		struct storage *storage = alloc_storage();

		/* FIXME! Totally made-up argument passing conventions */
		if (arg->type == PSEUDO_ARG) {
			storage->type = REG_ARG;
			storage->regno = arg->nr;
		}
		add_storage(storage, bb, arg, STOR_IN);
	} END_FOR_EACH_PTR(arg);
}

/*
 * One phi-source may feed multiple phi nodes. If so, combine
 * the storage output for this bb into one entry to reduce
 * storage pressure.
 */
static void combine_phi_storage(struct basic_block *bb)
{
	struct instruction *insn;
	FOR_EACH_PTR(bb->insns, insn) {
		struct instruction *phi, *last;

		if (!insn->bb || insn->opcode != OP_PHISOURCE)
			continue;
		last = NULL;
		FOR_EACH_PTR(insn->phi_users, phi) {
			if (last) {
				struct storage *s1, *s2;
				s1 = lookup_storage(bb, last->target, STOR_OUT);
				s2 = lookup_storage(bb, phi->target, STOR_OUT);
				if (s1 && s2 && s1 != s2)
					combine_storage(s1, s2);
			}
			last = phi;
			
		} END_FOR_EACH_PTR(phi);
	} END_FOR_EACH_PTR(insn);
}

void set_up_storage(struct entrypoint *ep)
{
	struct basic_block *bb;

	/* First set up storage for the incoming arguments */
	set_up_argument_storage(ep, ep->entry->bb);

	/* Then do a list of all the inter-bb storage */
	FOR_EACH_PTR(ep->bbs, bb) {
		set_up_bb_storage(bb);
		combine_phi_storage(bb);
	} END_FOR_EACH_PTR(bb);
}
