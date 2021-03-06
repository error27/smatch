/*
 * Copyright (C) 2017 Oracle.
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
#include "smatch_extra.h"

static int find_param_eq(struct expression *expr, int size)
{
	struct expression *arg;
	sval_t val;
	int i;

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		if (!get_implied_value(arg, &val))
			continue;
		if (val.value == size)
			return i;
	} END_FOR_EACH_PTR(arg);

	return -1;
}

static int find_skb_len(struct expression *call, struct expression *arg)
{
	struct expression *tmp;
	char buf[64];
	char *data_name, *len_name;
	int ret = -1;
	int len, i;

	data_name = expr_to_str(arg);
	if (!data_name)
		return -1;
	len = snprintf(buf, sizeof(buf), "%s", data_name);
	if (len < 4 || len >= sizeof(buf))
		goto free;
	sprintf(buf + len - 4, "len");

	i = -1;
	FOR_EACH_PTR(call->args, tmp) {
		bool match = false;

		i++;
		if (tmp == arg)
			continue;
		len_name = expr_to_var(tmp);
		if (!len_name)
			continue;
		if (strcmp(buf, len_name) == 0)
			match = true;
		free_string(len_name);
		if (match) {
			ret = i;
			goto free;
		}
	} END_FOR_EACH_PTR(tmp);

free:
	free_string(data_name);
	return ret;
}

static void match_call(struct expression *expr)
{
	struct expression *arg;
	struct symbol *type, *arg_type;
	int size, bytes;
	int i, nr;
	char buf[16];
	char elem_count[8];
	char byte_count[8];

	snprintf(elem_count, sizeof(elem_count), "%d", ELEM_COUNT);
	snprintf(byte_count, sizeof(byte_count), "%d", BYTE_COUNT);

	i = -1;
	FOR_EACH_PTR(expr->args, arg) {
		i++;
		type = get_type(arg);
		if (!type || (type->type != SYM_PTR && type->type != SYM_ARRAY))
			continue;

		if (is_skb_data(arg)) {
			nr = find_skb_len(expr, arg);
			if (nr >= 0) {
				snprintf(buf, sizeof(buf), "==$%d", nr);
				sql_insert_caller_info(expr, BYTE_COUNT, i, buf, byte_count);
				continue;
			}
		}
		bytes = get_array_size_bytes(arg);
		if (bytes > 0) {
			nr = find_param_eq(expr, bytes);
			if (nr >= 0) {
				snprintf(buf, sizeof(buf), "==$%d", nr);
				sql_insert_caller_info(expr, BYTE_COUNT, i, buf, byte_count);
				continue;
			}
		}
		arg_type = get_arg_type(expr->fn, i);
		if (arg_type != type)
			continue;

		size = get_array_size(arg);
		if (size > 0) {
			nr = find_param_eq(expr, size);
			if (nr >= 0) {
				snprintf(buf, sizeof(buf), "==$%d", nr);
				sql_insert_caller_info(expr, ELEM_COUNT, i, buf, elem_count);
				continue;
			}
		}
	} END_FOR_EACH_PTR(arg);
}

void register_passes_array_size(int id)
{
	add_hook(&match_call, FUNCTION_CALL_HOOK);
}

