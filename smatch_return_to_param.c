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

/*
 * This is for smatch_extra.c to use.  It sort of like check_assigned_expr.c but
 * more limited.  Say a function returns "64min-s64max[$0->data]" and the caller
 * does "struct whatever *p = get_data(dev);" then we want to record that p is
 * now the same as "dev->data".  Then if we update "p->foo" it means we can
 * update "dev->data->foo" as well.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;
static int link_id;

static struct smatch_state *alloc_my_state(const char *name, struct symbol *sym)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(name);
	state->data = sym;
	return state;
}

static void undef(struct sm_state *sm, struct expression *mod_expr)
{
	set_state(my_id, sm->name, sm->sym, &undefined);
}

char *map_call_to_other_name_sym(const char *name, struct symbol *sym, struct symbol **new_sym)
{
	struct smatch_state *state;
	int skip;
	char buf[256];

	/* skip 'foo->'.  This was checked in the caller. */
	skip = strlen(sym->ident->name) + 2;

	state = get_state(my_id, sym->ident->name, sym);
	if (!state || !state->data)
		return NULL;

	snprintf(buf, sizeof(buf), "%s->%s", state->name, name + skip);
	*new_sym = state->data;
	return alloc_string(buf);
}

/*
 * Normally, we expect people to consistently refer to variables by the shortest
 * name.  So they use "b->a" instead of "foo->bar.a" when both point to the
 * same memory location.  However, when we're dealing across function boundaries
 * then sometimes we pass frob(foo) which sets foo->bar.a.  In that case, we
 * translate it to the shorter name.  Smatch extra updates the shorter name,
 * which in turn updates the longer name.
 *
 */
char *map_long_to_short_name_sym(const char *name, struct symbol *sym, struct symbol **new_sym)
{
	struct sm_state *sm;
	int len;
	char buf[256];

	*new_sym = NULL;

	FOR_EACH_MY_SM(my_id, __get_cur_stree(), sm) {
		if (sm->state->data != sym)
			continue;
		len = strlen(sm->state->name);
		if (strncmp(name, sm->state->name, len) == 0) {
			if (name[len] == '.')
				continue;

			snprintf(buf, sizeof(buf), "%s%s", sm->name, name + len);
			*new_sym = sm->sym;
			return alloc_string(buf);
		}
	} END_FOR_EACH_SM(sm);

	return NULL;
}

static void store_mapping_helper(char *left_name, struct symbol *left_sym, struct expression *call, const char *return_string)
{
	const char *p = return_string;
	const char *end;
	int param;
	struct expression *arg;
	char *name = NULL;
	struct symbol *right_sym;
	char buf[256];

	while (*p && *p != '[')
		p++;
	if (!*p)
		goto free;
	p++;
	if (*p != '$')
		goto free;
	p++;
	param = atoi(p);
	p++;
	if (*p != '-' || *(p + 1) != '>')
		goto free;
	end = strchr(p, ']');
	if (!end)
		goto free;

	arg = get_argument_from_call_expr(call->args, param);
	if (!arg)
		goto free;
	name = expr_to_var_sym(arg, &right_sym);
	if (!name || !right_sym)
		goto free;
	snprintf(buf, sizeof(buf), "%s", name);

	if (end - p + strlen(buf) >= sizeof(buf))
		goto free;
	strncat(buf, p, end - p);

	set_state(my_id, left_name, left_sym, alloc_my_state(buf, right_sym));
	//set_state(my_id, buf, right_sym, alloc_my_state(left_name, left_sym));

	store_link(link_id, buf, right_sym, left_name, left_sym);

free:
	free_string(name);
}

void __add_return_to_param_mapping(struct expression *expr, const char *return_string)
{
	struct expression *call;
	char *left_name = NULL;
	struct symbol *left_sym;

	if (expr->type == EXPR_ASSIGNMENT) {
		left_name = expr_to_var_sym(expr->left, &left_sym);
		if (!left_name || !left_sym)
			goto free;

		call = strip_expr(expr->right);
		if (call->type != EXPR_CALL)
			goto free;

		store_mapping_helper(left_name, left_sym, call, return_string);
		goto free;
	}

free:
	free_string(left_name);
}

void register_return_to_param(int id)
{
	my_id = id;
	add_modification_hook(my_id, &undef);
}

void register_return_to_param_links(int id)
{
	link_id = id;
	set_up_link_functions(my_id, link_id);
}

