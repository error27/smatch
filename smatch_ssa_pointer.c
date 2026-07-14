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
static unsigned long ssa_id = 1;
static struct stree *has_ssa;

static struct smatch_state *ssa_ptr_member(const char *name)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(name);

	return state;
}

static struct smatch_state *ssa_ptr_new(const char *name)
{
	struct smatch_state *state;
	char buf[64];

	state = __alloc_smatch_state(0);
	snprintf(buf, sizeof(buf), "%s{%ld}", name, ssa_id);
	state->name = alloc_sname(buf);

	ssa_id++;

	return state;
}

static struct sm_state *get_ssa_ptr_sm(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	bool amp = false;
	int len;

	if (!name || !sym)
		return NULL;

	if (name[0] == '&') {
		name++;
		amp = true;
	}

	sm = get_sm_state(my_id, name, sym);
	if (sm) {
		if (sm->state == &undefined || sm->state == &merged)
			return NULL;
		return sm;
	}

	FOR_EACH_SM_REVERSE(has_ssa, sm) {
		if (sm->sym != sym)
			continue;
		if (sm->state == &undefined || sm->state == &merged)
			return NULL;
		len = strlen(sm->name);
		if (strncmp(sm->name, name, len) != 0)
			continue;
		if (name[len] == '-' || name[len] == '.')
			goto found;
		if (amp && name[len] == '\0')
			goto found;
	} END_FOR_EACH_SM(sm);

	return NULL;

found:
	sm = get_sm_state(my_id, sm->name, sm->sym);
	if (!sm || sm->state == &undefined || sm->state == &merged)
		return NULL;
	return sm;
}

const char *get_ssa_ptr_name_sym(const char *name, struct symbol *sym)
{
	struct sm_state *sm;
	char buf[128];
	int len;

	sm = get_ssa_ptr_sm(name, sym);
	if (!sm)
		return NULL;

	len = strlen(sm->name);
	if (strlen(name) < len) {
		sm_perror("unexpected ssa ptr length name=%s sm->name=%s state->name=%s, len=%d",
			  name, sm->name, sm->state->name, len);
		return NULL;
	}
	if (name[len] == '\0')
		return sm->state->name;
	if (name[len] == '-')
		snprintf(buf, sizeof(buf), "%s%s", sm->state->name, name + len);
	else
		snprintf(buf, sizeof(buf), "%s->%s", sm->state->name, name + len + 1);

	return alloc_sname(buf);

}

const char *get_ssa_ptr_name(struct expression *expr)
{
	struct symbol *sym;
	const char *ret;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return NULL;

	ret = get_ssa_ptr_name_sym(name, sym);
	free_string(name);
	return ret;
}

static void store_ssa_state(struct expression *expr, struct smatch_state *state)
{
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return;

	set_state_stree(&has_ssa, my_id, name, sym, state);
	set_state(my_id, name, sym, state);
	free_string(name);
}

static struct smatch_state *get_or_alloc_ssa_ptr(struct expression *expr)
{
	struct smatch_state *state;
	const char *ssa_name;
	struct sm_state *sm;
	struct symbol *type;
	struct symbol *sym;
	char *name;

	if (!expr)
		return NULL;

	type = get_type(expr);
	if (!type || type->type != SYM_PTR)
		return NULL;
	type = get_real_base_type(type);
	if (!type || type->type != SYM_STRUCT)
		return NULL;

	if (expr->type == EXPR_PREOP && expr->op == '&')
		expr = strip_expr(expr->unop);

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return NULL;

	sm = get_ssa_ptr_sm(name, sym);
	if (!sm) {
		state = ssa_ptr_new(name);
		free_string(name);
		store_ssa_state(expr, state);
		return state;
	}
	if (strcmp(sm->name, name) == 0) {
		free_string(name);
		return sm->state;
	}
	ssa_name = get_ssa_ptr_name_sym(name, sym);
	state = ssa_ptr_member(ssa_name);
	free_string(name);
	store_ssa_state(expr, state);
	return state;
}

static void match_assign(struct expression *expr)
{
	struct smatch_state *state;
	struct symbol *type;

	if (expr->op != '=')
		return;
	if (__in_fake_struct_assign)
		return;

	type = get_type(expr->left);
	if (!is_ptr_type(type))
		return;

	state = get_or_alloc_ssa_ptr(expr->right);
	if (!state && expr->right && expr->right->type == EXPR_CALL) {
		char *name;

		name = expr_to_var(expr->left);
		if (!name)
			return;
		state = ssa_ptr_new(name);
		free_string(name);
		store_ssa_state(expr->left, state);
		return;
	}
	if (!state)
		return;
	store_ssa_state(expr->left, state);
}

void smatch_ssa_pointer(int id)
{
	my_id = id;

	add_function_data((unsigned long *)&has_ssa);

	set_dynamic_states(my_id);
	add_modification_hook(my_id, &set_undefined);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
