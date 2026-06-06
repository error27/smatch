/*
 * Copyright 2025 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include "smatch.h"

static int my_id;

static int db_info(void *_value, int argc, char **argv, char **azColName)
{
	char **value = _value;

	if (*value) {
		if (strcmp(*value, argv[0]) == 0)
			return 0;
		free_string(*value);
		*value = alloc_string("unknown");
		return 0;

	}
	*value = alloc_string(argv[0]);
	return 0;
}

bool get_member_type_info(const char *member, int type, char **value)
{
	char *db_val = NULL;

	if (!member)
		return false;

	cache_sql(&db_info, &db_val, "select value from type_info where type = %d and key = '%s';",
		  type, member);
	if (db_val)
		goto found;
	run_sql(&db_info, &db_val, "select value from type_info where type = %d and key = '%s';",
		type, member);
	if (!db_val)
		return false;

found:
	*value = db_val;
	return true;
}

bool get_type_info(struct expression *expr, int type, char **value)
{
	char *member;

	*value = NULL;

	member = get_member_name(expr);
	if (!member)
		return false;
	return get_member_type_info(member, type, value);
}

void add_member_type_info(const char *member_name, int type, const char *value)
{
	if (!member_name)
		return;
	sql_insert_cache(type_info, "0x%llx, %d, '%s', '%s'",
			 get_base_file_id(), type, member_name, value);
}

void add_type_info(struct expression *expr, int type, const char *value)
{
	char *member_name;

	member_name = get_member_name(expr);
	if (!member_name)
		return;

	add_member_type_info(member_name, type, value);
}

void smatch_type_info(int id)
{
	my_id = id;
}
