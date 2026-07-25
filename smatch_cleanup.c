/*
 * Copyright (C) 2026 Dan Carpenter.
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

static void match_declarations(struct symbol *sym)
{
	if (!cur_func_sym)
		return;
	if (!sym->ident)
		return;

	if (!sym->cleanup)
		return;

	set_state(my_id, sym->ident->name, sym, &true_state);
}

void __call_cleanup_fn(struct sm_state *sm)
{
	struct expression *call, *arg;
	struct expression_list *args = NULL;

	arg = symbol_expression(sm->sym);
	arg = preop_expression(arg, '&');
	add_ptr_list(&args, arg);
	call = call_expression(sm->sym->cleanup, args);
	call->pos.line = get_lineno();

	__split_expr(call);
}

void smatch_cleanup(int id)
{
	my_id = id;

	disable_ssa_pointers(id);
	add_hook(&match_declarations, DECLARATION_HOOK);
}
