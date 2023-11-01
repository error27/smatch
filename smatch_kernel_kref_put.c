/*
 * Copyright 2023 Linaro Ltd.
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
#include "smatch_slist.h"

static int my_id;

static bool has_inc_state(const char *name, struct symbol *sym)
{
	static int refcount_id;
	struct sm_state *sm, *tmp;

	if (!refcount_id)
		refcount_id = id_from_name("check_refcount_info");

	sm = get_sm_state(refcount_id, name, sym);
	if (!sm)
		return false;

	FOR_EACH_PTR(sm->possible, tmp) {
		if (strcmp(tmp->state->name, "inc") == 0)
			return true;
		/*
		 * &ignore counts as an inc, because that's what happens when
		 * you double increment.  Not ideal.
		 */
		if (strcmp(tmp->state->name, "ignore") == 0)
			return true;
	} END_FOR_EACH_PTR(tmp);

	return false;
}

static void match_kref_put(const char *fn, struct expression *call_expr,
			   struct expression *expr, void *_unused)
{
	struct expression *data, *release, *fake_call;
	struct expression_list *args = NULL;
	struct symbol *sym;
	char *ref;


	if (call_expr->type != EXPR_CALL)
		return;

	data = get_argument_from_call_expr(call_expr->args, 0);

	ref = get_name_sym_from_param_key(call_expr, 0, "$->refcount.refs.counter", &sym);
	if (has_inc_state(ref, sym))
		return;

	release = get_argument_from_call_expr(call_expr->args, 1);

	add_ptr_list(&args, data);
	fake_call = call_expression(release, args);
	add_fake_call_after_return(fake_call);
}

void register_kernel_kref_put(int id)
{
	if (option_project != PROJ_KERNEL)
		return;

	my_id = id;
	return_implies_state_sval("kref_put", int_one, int_one, &match_kref_put, NULL);
}
