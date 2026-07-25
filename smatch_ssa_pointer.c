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
static struct stree *has_ssa;
static char *disable_ssa;

static struct smatch_state *ssa_ptr_member(const char *name)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	state->name = alloc_sname(name);

	return state;
}

static void gen_name(char *buf, size_t len, struct expression *expr, const char *name)
{
	struct expression *tmp;

	if (expr->type == EXPR_PREOP && expr->op == '&') {
		tmp = strip_expr(expr->unop);
		if (tmp && tmp->type == EXPR_SYMBOL) {
			snprintf(buf, len, "%s{0}", name);
			return;
		}
	}
	snprintf(buf, len, "%s{%d_%p}", name, expr->pos.line, expr);
}

static struct smatch_state *ssa_ptr_new(struct expression *expr, const char *name)
{
	struct smatch_state *state;
	char buf[64];

	state = __alloc_smatch_state(0);
	gen_name(buf, sizeof(buf), expr, name);

	state->name = alloc_sname(buf);

	return state;
}

static struct sm_state *get_ssa_ptr_sm(const char *name, struct symbol *sym)
{
	static bool nested;
	const char *dot, *arrow;
	struct sm_state *sm;
	int len;

	if (!name || !sym)
		return NULL;

	if (nested)
		return NULL;
	nested = true;

	sm = get_sm_state(my_id, name, sym);
	if (sm)
		goto done;

	FOR_EACH_SM_REVERSE(has_ssa, sm) {
		if (sm->sym != sym)
			continue;
		if (sm->state == &undefined || sm->state == &merged)
			goto done;
		len = strlen(sm->name);
		if (strncmp(sm->name, name, len) != 0)
			continue;
		if (name[len] == '-' || name[len] == '.')
			goto found;
		if (name[len] == '\0')
			goto found;
	} END_FOR_EACH_SM(sm);

	dot = strchr(name, '.');
	if (dot) {
		arrow = strchr(name, '-');
		if (!arrow || dot < arrow) {
			char buf[64];

			snprintf(buf, sizeof(buf), "&%.*s", (int)(dot - name), name);
			sm = get_sm_state(my_id, buf, sym);
			goto done;
		}
	}

	sm = NULL;
	goto done;

found:
	sm = get_sm_state(my_id, sm->name, sm->sym);
done:
	if (sm && (sm->state == &undefined || sm->state == &merged))
		sm = NULL;
	nested = false;
	return sm;
}

static const char *expand_ssa_name(struct sm_state *sm, const char *name)
{
	char buf[128];
	int amp = 0;
	int len;

	if (!sm)
		return NULL;
	if (sm->name[0] == '&')
		amp = 1;

	len = strlen(sm->name);
	if (strlen(name) < len) {
		sm_perror("unexpected ssa ptr length name=%s sm->name=%s state->name=%s, len=%d",
			  name, sm->name, sm->state->name, len);
		return NULL;
	}
	if (name[len] == '\0')
		return sm->state->name;
	if (name[len] == '-')
		snprintf(buf, sizeof(buf), "%s%s", sm->state->name, name + len - amp);
	else
		snprintf(buf, sizeof(buf), "%s->%s", sm->state->name, name + len - amp + 1);

	return alloc_sname(buf);
}

const char *get_ssa_ptr_name_sym(const char *name, struct symbol *sym)
{
	struct sm_state *sm;

	sm = get_ssa_ptr_sm(name, sym);
	if (!sm)
		return NULL;

	return expand_ssa_name(sm, name);
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

static struct sm_state *store_ssa_state(struct expression *expr, struct smatch_state *state)
{
	struct sm_state *sm;
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return NULL;

	set_state_stree(&has_ssa, my_id, name, sym, state);
	sm = set_state(my_id, name, sym, state);
	free_string(name);
	return sm;
}

static void promote_states_to_ssa(struct sm_state *sm)
{
	struct state_list *slist = NULL;
	struct sm_state *tmp, *new;
	const char *ssa_name;


	if (!sm)
		return;

	FOR_EACH_SM(__get_cur_stree(), tmp) {
		if (tmp->sym != sm->sym)
			continue;
		if (ssa_pointers_disabled(tmp->owner))
			continue;
		if (get_ssa_ptr_sm(tmp->name, tmp->sym) != sm)
			continue;
		ssa_name = expand_ssa_name(sm, tmp->name);
		if (!ssa_name)
			continue;

		new = clone_sm(tmp);
		new->name = ssa_name;
		new->sym = NULL;
		add_ptr_list(&slist, new);
	} END_FOR_EACH_SM(tmp);

	FOR_EACH_PTR(slist, tmp) {
		__set_sm(tmp);
	} END_FOR_EACH_PTR(tmp);

	free_slist(&slist);
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

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return NULL;

	sm = get_ssa_ptr_sm(name, sym);
	if (!sm) {
		state = ssa_ptr_new(expr, name);
		free_string(name);
		sm = store_ssa_state(expr, state);
		promote_states_to_ssa(sm);
		return sm ? sm->state : NULL;
	}
	if (strcmp(sm->name, name) == 0) {
		free_string(name);
		return sm->state;
	}
	ssa_name = get_ssa_ptr_name_sym(name, sym);
	state = ssa_ptr_member(ssa_name);
	free_string(name);
	sm = store_ssa_state(expr, state);
	promote_states_to_ssa(sm);
	return sm ? sm->state : NULL;
}

static struct expression *ignored_mod;
static void match_assign(struct expression *expr)
{
	struct smatch_state *state;
	struct symbol *type;

	if (expr->op != '=')
		return;
	if (__in_fake_struct_assign)
		return;
	if (__in_fake_parameter_assign)
		return;

	if (expr->left->smatch_flags & Fake)
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
		state = ssa_ptr_new(expr->right, name);
		free_string(name);
		store_ssa_state(expr->left, state);
		ignored_mod = expr;
		return;
	}
	if (!state)
		return;
	ignored_mod = expr;
	store_ssa_state(expr->left, state);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	struct smatch_state *state;
	sval_t sval;

	if (sm->name[0] == '&' && sm->sym && sm->sym->ident &&
	    strcmp(sm->sym->ident->name, sm->name + 1) == 0)
		return sm->state;

	state = get_extra_name_sym(sm->name, sm->sym);
	if (estate_get_single_value(state, &sval) && sval.value == 0)
		return sm->state;
	return &undefined;
}

static struct smatch_state *merge_states(struct smatch_state *s1, struct smatch_state *s2)
{
	if (strcmp(s1->name, s2->name) == 0)
		return s1;
	return &merged;
}

static void match_modify(struct sm_state *sm, struct expression *mod_expr)
{
	if (ignored_mod && mod_expr == ignored_mod)
		return;
	set_state(sm->owner, sm->name, sm->sym, &undefined);
}

void disable_ssa_pointers(int id)
{
	disable_ssa[id] = true;
}

bool ssa_pointers_disabled(int owner)
{
	if (owner >= 0 && owner < num_checks)
		return disable_ssa[owner];
	return false;
}

void smatch_ssa_pointer(int id)
{
	my_id = id;

	disable_ssa = malloc(num_checks);
	memset(disable_ssa, 0, num_checks);

	disable_ssa_pointers(my_id);
	add_function_data((unsigned long *)&has_ssa);

	set_dynamic_states(my_id);
	add_modification_hook(my_id, &match_modify);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_merge_hook(my_id, &merge_states);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
}
