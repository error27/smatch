/*
 * sparse/smatch_function_hashtable.c
 *
 * Copyright (C) 2010 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "smatch.h"
#include "cwchash/hashtable.h"

static inline unsigned int djb2_hash(void *ky)
{
	char *str = (char *)ky;
	unsigned long hash = 5381;
	int c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

        return hash;
}

static inline int equalkeys(void *k1, void *k2)
{
	return !strcmp((char *)k1, (char *)k2);
}

#define DEFINE_FUNCTION_ADD_HOOK(_name, _item_type, _list_type) \
void add_##_name(struct hashtable *table, const char *look_for, _item_type *value) \
{                                                               \
	_list_type *list;                                       \
	char *key;                                              \
                                                                \
	key = alloc_string(look_for);                           \
	list = search_##_name(table, key);                      \
	if (!list) {                                            \
		add_ptr_list(&list, value);                     \
	} else {                                                \
		remove_##_name(table, key);                     \
		add_ptr_list(&list, value);                     \
	}                                                       \
	insert_##_name(table, key, list);                       \
}

static inline struct hashtable *create_function_hashtable(int size)
{
	return create_hashtable(size, djb2_hash, equalkeys);
}

static inline void destroy_function_hashtable(struct hashtable *table)
{
	hashtable_destroy(table, 0);
}

#define DEFINE_FUNCTION_HASHTABLE(_name, _item_type, _list_type)   \
	DEFINE_HASHTABLE_INSERT(insert_##_name, char, _list_type); \
	DEFINE_HASHTABLE_SEARCH(search_##_name, char, _list_type); \
	DEFINE_HASHTABLE_REMOVE(remove_##_name, char, _list_type); \
	DEFINE_FUNCTION_ADD_HOOK(_name, _item_type, _list_type);

#define DEFINE_FUNCTION_HASHTABLE_STATIC(_name, _item_type, _list_type)   \
	static DEFINE_HASHTABLE_INSERT(insert_##_name, char, _list_type); \
	static DEFINE_HASHTABLE_SEARCH(search_##_name, char, _list_type); \
	static DEFINE_HASHTABLE_REMOVE(remove_##_name, char, _list_type); \
	static DEFINE_FUNCTION_ADD_HOOK(_name, _item_type, _list_type);
