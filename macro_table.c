/*
 * smatch/macro_table.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "lib.h"
#include "parse.h"
#include "cwchash/hashtable.h"

static struct hashtable *macro_table;

static DEFINE_HASHTABLE_INSERT(do_insert_macro, struct position, char);
static DEFINE_HASHTABLE_SEARCH(do_search_macro, struct position, char);

static inline unsigned int position_hash(void *_pos)
{
	struct position *pos = _pos;

	return pos->line | (pos->pos << 22); 
}

static inline int equalkeys(void *_pos1, void *_pos2)
{
	struct position *pos1 = _pos1;
	struct position *pos2 = _pos2;

	return pos1->line == pos2->line && pos1->pos == pos2->pos;
}

void store_macro_pos(struct token *token)
{
	if (!macro_table)
		macro_table = create_hashtable(5000, position_hash, equalkeys);

	do_insert_macro(macro_table, &token->pos, token->ident->name);
}

char *get_macro_name(struct position *pos)
{
	return do_search_macro(macro_table, pos);
}
