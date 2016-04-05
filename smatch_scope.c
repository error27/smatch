/*
 * Copyright (C) 2016 Oracle.
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

static int end_of_function(struct statement *stmt)
{
	struct symbol *fn = get_base_type(cur_func_sym);

	/* err on the conservative side of things */
	if (!fn)
		return 1;
	if (stmt == fn->stmt || stmt == fn->inline_stmt)
		return 1;
	return 0;
}

/*
 * We're wasting a lot of time worrying about out of scope variables.
 * When we come to the end of a scope then just delete them all the out of
 * scope states.
 */
static void match_end_of_block(struct statement *stmt)
{
	struct statement *tmp;
	struct symbol *sym;

	if (stmt->type != STMT_COMPOUND)
		return;

	if (end_of_function(stmt))
		return;

	FOR_EACH_PTR(stmt->stmts, tmp) {
		if (tmp->type != STMT_DECLARATION)
			return;

		FOR_EACH_PTR(tmp->declaration, sym) {
			if (!sym->ident)
				continue;
			__delete_all_states_sym(sym);
		} END_FOR_EACH_PTR(sym);
	} END_FOR_EACH_PTR(tmp);
}

void register_scope(int id)
{
	add_hook(&match_end_of_block, STMT_HOOK_AFTER);
}
