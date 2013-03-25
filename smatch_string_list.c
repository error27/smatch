/*
 * sparse/smatch_string_list.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

int list_has_string(struct string_list *str_list, char *str)
{
	char *tmp;

	FOR_EACH_PTR(str_list, tmp) {
		if (strcmp(tmp, str) < 0)
			continue;
		if (strcmp(tmp, str) == 0)
			return 1;
		return 0;
	} END_FOR_EACH_PTR(tmp);
	return 0;
}

void insert_string(struct string_list **str_list, char *new)
{
	char *tmp;

	FOR_EACH_PTR(*str_list, tmp) {
		if (strcmp(tmp, new) < 0)
			continue;
		else if (strcmp(tmp, new) == 0) {
			return;
		} else {
			INSERT_CURRENT(new, tmp);
			return;
		}
	} END_FOR_EACH_PTR(tmp);
	add_ptr_list(str_list, new);
}

struct string_list *clone_str_list(struct string_list *orig)
{
	char *tmp;
	struct string_list *ret = NULL;

	FOR_EACH_PTR(orig, tmp) {
		add_ptr_list(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}

struct string_list *combine_string_lists(struct string_list *one, struct string_list *two)
{
	struct string_list *ret;
	char *tmp;

	ret = clone_str_list(one);
	FOR_EACH_PTR(two, tmp) {
		insert_string(&ret, tmp);
	} END_FOR_EACH_PTR(tmp);
	return ret;
}


