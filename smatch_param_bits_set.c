/*
 * Copyright (C) 2021 Oracle
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
#include "scope.h"
#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

void __set_param_modified_helper(struct expression *expr, struct  smatch_state *state)
{
	set_state_expr(my_id, expr, state);
}

void __set_param_modified_helper_sym(const char *name, struct symbol *sym,
				     struct smatch_state *state)
{
	set_state(my_id, name, sym, state);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	return alloc_bstate(0, 0);
}

static void return_info_callback(int return_id, char *return_ranges,
				 struct expression *returned_expr,
				 int param,
				 const char *printed_name,
				 struct sm_state *sm)
{
	unsigned long long bits_set;
	char buffer[64];
	struct smatch_state *estate;
	struct bit_info *binfo;
	sval_t sval;

	estate = get_state(SMATCH_EXTRA, sm->name, sm->sym);
	if (estate_get_single_value(estate, &sval))
		return;

	binfo = sm->state->data;
	bits_set = binfo->set;

	if (bits_set == 0)
		return;

	sprintf(buffer, "0x%llx", bits_set);
	sql_insert_return_states(return_id, return_ranges, BIT_SET, param, printed_name, buffer);
}

void register_param_bits_set(int id)
{
	my_id = id;

	set_dynamic_states(my_id);

	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_bstates);

	add_return_info_callback(my_id, return_info_callback);
}
