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

#include <ctype.h>
#include <stdlib.h>

#include "smatch.h"

static int my_id;
static struct stree *has_ssa, *ssa_to_vs;
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
			snprintf(buf, len, "%s{}", name);
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
	const char *dot, *arrow, *no_amp;
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

		len = strlen(sm->name);
		if (name[0] == '&' && sm->name[0] != '&') {
			no_amp = name + 1;
			if (strncmp(sm->name, no_amp, len) != 0)
				continue;
			if (no_amp[len] == '-' || no_amp[len] == '.')
				goto found;
			if (no_amp[len] == '\0')
				goto found;
		}
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

static const char *handle_struct_swap(struct sm_state *sm, const char *name)
{
	char buf[128];

	/* This converts "foo.a" into "(&foo{})->a". */

	if (sm->name[0] != '&' || sm->state->name[0] != '&')
		return 0;
	if (name[0] == '&')
		return NULL;
	if (!sm->sym || !sm->sym->ident)
		return NULL;

	if (strncmp(sm->state->name + 1, sm->sym->ident->name, sm->sym->ident->len) != 0)
		return NULL;
	if (sm->state->name[1 + sm->sym->ident->len] != '{')
		return NULL;
	if (strncmp(name, sm->sym->ident->name, sm->sym->ident->len) != 0)
		return NULL;
	if (name[sm->sym->ident->len] != '.')
		return NULL;

	snprintf(buf, sizeof(buf), "(%s)->%s", sm->state->name, name + 1 + sm->sym->ident->len);
	return alloc_sname(buf);
}

static const char *expand_ssa_name(struct sm_state *sm, const char *name)
{
	const char *ret;
	char buf[128];
	int amp = 0;
	int len;

	if (!sm)
		return NULL;

	/* The sm is something like "p equals &foo{}" and the name
	 * is something like "p->a".  And we want to translate that to
	 * "(&foo{})->a".
	 *
	 * There are a few scenarios:
	 * foo.a becomes &foo{}->a
	 * &p->a->stuff becomes &(&foo{})->a->stuff
	 *
	 */

	ret = handle_struct_swap(sm, name);
	if (ret)
		return ret;

	len = strlen(sm->name);
	if (strlen(name) < len + amp) {
		sm_msg("unexpected ssa ptr length name=%s sm->name=%s state->name=%s, len=%d amp=%d",
			  name, sm->name, sm->state->name, len, amp);
		return NULL;
	}
	if (name[len] == '\0')
		return sm->state->name;
	if (name[len] == '-')
		snprintf(buf, sizeof(buf), "%s%s%s%s",
			 sm->state->name[0] == '&' ? "(" : "",
			 sm->state->name,
			 sm->state->name[0] == '&' ? ")" : "",
			 name + len + amp);
	else if (name[len] == '.')
		snprintf(buf, sizeof(buf), "%s%s%s->%s",
			 sm->state->name[0] == '&' ? "(" : "",
			 sm->state->name,
			 sm->state->name[0] == '&' ? ")" : "",
			 name + len + amp + 1);
	else
		snprintf(buf, sizeof(buf), "%s->%s", sm->state->name, name + len - amp + 1);

	return alloc_sname(buf);
}

const char *swap_ssa_ptr_to_name_sym(struct expression *expr, const char *name, struct symbol **sym)
{
	const char *ssa_name;
	struct symbol *var_sym;
	char *var_name;
	const char *p;
	char buf[64];
	int len;

	if (!expr || *sym)
		return name;
	if (!strchr(name, '{'))
		return name;

	ssa_name = get_ssa_ptr_name(expr);
	if (!ssa_name)
		return name;

	p = strstr(name, ssa_name);
	if (!p)
		return name;
	len = strlen(ssa_name);

	var_name = expr_to_var_sym(expr, &var_sym);
	if (!var_name)
		return name;

	snprintf(buf, sizeof(buf), "%.*s%s%s",
		 (int)(p - name), name, var_name, name + len);

	*sym = var_sym;
	return alloc_sname(buf);
}

bool ssa_buf_contains(const char *container, const char *var)
{
	int skip = 0;
	int i;

	if (!container || !var)
		return false;

	if (var[0] == '(')
		skip++;

	i = 0;
	while (container[i] && container[i] == var[i + skip])
		i++;

	if (container[i] != '\0')
		return false;

	var += i + skip;
	if (skip) {
		if (var[0] != ')' || var[1] != '-')
			return false;
	} else {
		if (var[0] != '-')
			return false;
	}
	return true;
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

static struct var_sym_list *ssa_to_vsl(const char *ssa_name)
{
	struct smatch_state *state;

	state = get_state_stree(ssa_to_vs, my_id, ssa_name, NULL);
	if (!state)
		return NULL;
	return state->data;
}

const char *filter_ssa_names(const char *name)
{
	struct var_sym_list *vsl;
	struct var_sym *vs;

	vsl = ssa_to_vsl(name);
	if (!vsl)
		return name;
	vs = first_ptr_list((struct ptr_list *)vsl);
	return vs->var;
}

static void store_ssa_to_vs(const char *ssa_name, const char *name, struct symbol *sym)
{
	struct smatch_state *state;
	struct var_sym_list *vsl;
	struct var_sym *vs;

	if (!ssa_name || !name || !sym)
		return;

	vs = alloc_var_sym(name, sym);
	state = get_state_stree(ssa_to_vs, my_id, ssa_name, NULL);
	if (!state) {
		state = __alloc_smatch_state(0);
		state->name = alloc_sname(name);
	}

	vsl = state->data;
	add_ptr_list(&vsl, vs);
	state->data = vsl;
	set_state_stree(&ssa_to_vs, my_id, ssa_name, NULL, state);
}

static struct sm_state *store_ssa_state(struct expression *expr, struct smatch_state *state)
{
	struct sm_state *sm;
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	if (!name)
		return NULL;

	store_ssa_to_vs(state->name, name, sym);
	set_state_stree(&has_ssa, my_id, name, sym, state);
	sm = set_state(my_id, name, sym, state);
	free_string(name);
	return sm;
}

static void store_ssa_name_sym(const char *name, struct symbol *sym, struct smatch_state *state)
{
	set_state_stree(&has_ssa, my_id, name, sym, state);
	set_state(my_id, name, sym, state);
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
	if (!ssa_name)
		return NULL;
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
	if (__in_fake_assign || __in_fake_struct_assign)
		return;

	if (expr->left->smatch_flags & Fake)
		return;

	type = get_type(expr->left);
	if (!is_ptr_type(type))
		return;

	if (expr_equiv(expr->left, expr->right))
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

static void match_function_def(struct symbol *sym)
{
	struct smatch_state *state;
	struct symbol *type;
	struct symbol *arg;
	char buf[64];
	int i;

	i = -1;
	FOR_EACH_PTR(cur_func_sym->ctype.base_type->arguments, arg) {
		i++;
		if (!arg->ident)
			continue;

		type = get_real_base_type(arg);
		if (!type || type->type != SYM_PTR)
			continue;

		state = __alloc_smatch_state(0);
		snprintf(buf, sizeof(buf), "%s{%d}", arg->ident->name, i);
		state->name = alloc_sname(buf);
		store_ssa_name_sym(arg->ident->name, arg, state);
	} END_FOR_EACH_PTR(arg);
}

static void match_declaration(struct symbol *sym)
{
	struct smatch_state *state;
	struct symbol *type;
	char buf[64];

	if (!sym->ident)
		return;

	type = get_real_base_type(sym);
	if (!type || type->type != SYM_STRUCT)
		return;

	state = __alloc_smatch_state(0);
	snprintf(buf, sizeof(buf), "&%s{}", sym->ident->name);
	state->name = alloc_sname(buf);

	snprintf(buf, sizeof(buf), "&%s", sym->ident->name);
	store_ssa_name_sym(buf, sym, state);
}

static struct smatch_state *unmatched_state(struct sm_state *sm)
{
	if (sm->name[0] == '&' && sm->sym && sm->sym->ident &&
	    strcmp(sm->sym->ident->name, sm->name + 1) == 0)
		return sm->state;

	return &undefined;
}

static void pre_merge_hook(struct sm_state *cur, struct sm_state *other)
{
	struct smatch_state *state;

	state = get_extra_name_sym(cur->name, cur->sym);
	if (!state)
		return;
	if (!rl_intersection(valid_ptr_rl, estate_rl(state)))
		set_state(my_id, cur->name, cur->sym, other->state);
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

static void free_resources(struct symbol *sym)
{
	free_stree(&has_ssa);
	free_stree(&ssa_to_vs);
}

void smatch_ssa_pointer(int id)
{
	my_id = id;

	disable_ssa = malloc(num_checks);
	memset(disable_ssa, 0, num_checks);

	disable_ssa_pointers(my_id);
	add_function_data((unsigned long *)&has_ssa);
	add_function_data((unsigned long *)&ssa_to_vs);

	set_dynamic_states(my_id);
	add_modification_hook(my_id, &match_modify);
	add_unmatched_state_hook(my_id, &unmatched_state);
	add_pre_merge_hook(my_id, &pre_merge_hook);
	add_merge_hook(my_id, &merge_states);
	add_hook(&match_assign, ASSIGNMENT_HOOK);
	add_hook(&match_function_def, FUNC_DEF_HOOK);
	add_hook(&match_declaration, DECLARATION_HOOK);
	add_hook(&free_resources, AFTER_PASS0_HOOK);
	add_hook(&free_resources, AFTER_FUNC_HOOK);
}
