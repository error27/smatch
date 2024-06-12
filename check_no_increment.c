/*
 * Copyright 2024 Linaro Ltd.
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
#include "smatch_slist.h"

STATE(start);
STATE(iterator);
STATE(modified);

static int my_id;

struct stree *stree;

static void set_inc_state(const char *name, struct symbol *sym, struct smatch_state *state)
{
	/*
	 * Call set_state() to make the modified hook work, but then call
	 * set_state_stree() to deal with backward gotos.
	 */

	set_state(my_id, sym->ident->name, sym, state);
	set_state_stree(&stree, my_id, sym->ident->name, sym, state);
}

static void match_declaration(struct symbol *sym)
{
	struct symbol *type;

	if (!sym->ident)
		return;

	if (strcmp(sym->ident->name, "i") != 0 &&
	    strcmp(sym->ident->name, "j") != 0)
		return;

	type = get_real_base_type(sym);
	if (!type || type->type != SYM_BASETYPE)
		return;

	set_inc_state(sym->ident->name, sym, &start);
}

static void match_modify(struct sm_state *sm, struct expression *expr)
{
	sval_t dummy;
	struct sm_state *old;

	old = get_sm_state_stree(stree, my_id, sm->name, sm->sym);
	if (old && slist_has_state(old->possible, &modified))
		return;

	if (expr &&
	    expr->type == EXPR_ASSIGNMENT && expr->op == '=' &&
	    get_value(expr->right, &dummy) && dummy.value == 0) {
		set_inc_state(sm->name, sm->sym, &iterator);
		return;
	}


	set_inc_state(sm->name, sm->sym, &modified);
}

static void process_states(void)
{
	struct sm_state *tmp;

	if (__bail_on_rest_of_function)
		return;

	FOR_EACH_SM(stree, tmp) {
		if (slist_has_state(tmp->possible, &iterator) &&
		    !slist_has_state(tmp->possible, &modified))
			sm_warning_line(tmp->line, "iterator '%s' not incremented", tmp->name);
	} END_FOR_EACH_SM(tmp);
}

static void clear_stree(void)
{
	free_stree(&stree);
}

void check_no_increment(int id)
{
	my_id = id;

	add_function_data((unsigned long *)&stree);

	preserve_out_of_scope(my_id);
	add_hook(&clear_stree, AFTER_FUNC_HOOK);

	add_hook(&match_declaration, DECLARATION_HOOK);
	add_modification_hook(my_id, &match_modify);

	all_return_states_hook(&process_states);
}
