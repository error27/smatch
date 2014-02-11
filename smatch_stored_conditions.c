/*
 * Copyright (C) 2014 Oracle.
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
 * Keep a record of all the things we have tested for so that we know when we
 * test for it again.  For example, if we have code like this:
 *
 * 	if (foo & FLAG)
 *		lock();
 *
 *	...
 *
 *	if (foo & FLAG)
 *		unlock();
 *
 * That's the end goal at least.  But actually implementing the flow of that
 * requires quite a bit of work because if "foo" changes the condition needs to
 * be retested and smatch_implications.c needs to be updated.
 *
 * For now, I just record the conditions and use it to see if we test for NULL
 * twice.
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;
static int link_id;

static struct smatch_state *alloc_link_state(struct string_list *links)
{
	struct smatch_state *state;
	static char buf[256];
	char *tmp;
	int i;

	state = __alloc_smatch_state(0);

	i = 0;
	FOR_EACH_PTR(links, tmp) {
		if (!i++) {
			snprintf(buf, sizeof(buf), "%s", tmp);
		} else {
			append(buf, ", ", sizeof(buf));
			append(buf, tmp, sizeof(buf));
		}
	} END_FOR_EACH_PTR(tmp);

	state->name = alloc_sname(buf);
	state->data = links;
	return state;
}

static struct smatch_state *merge_links(struct smatch_state *s1, struct smatch_state *s2)
{
	struct smatch_state *ret;
	struct string_list *links;

	links = combine_string_lists(s1->data, s2->data);
	ret = alloc_link_state(links);
	return ret;
}

static void save_link_var_sym(const char *var, struct symbol *sym, const char *link)
{
	struct smatch_state *old_state, *new_state;
	struct string_list *links;
	char *new;

	old_state = get_state(link_id, var, sym);
	if (old_state)
		links = clone_str_list(old_state->data);
	else
		links = NULL;

	new = alloc_sname(link);
	insert_string(&links, new);

	new_state = alloc_link_state(links);
	set_state(link_id, var, sym, new_state);
}

static void match_modify(struct sm_state *sm, struct expression *mod_expr)
{
	struct string_list *links;
	char *tmp;

	links = sm->state->data;

	FOR_EACH_PTR(links, tmp) {
		set_state(my_id, tmp, NULL, &undefined);
	} END_FOR_EACH_PTR(tmp);
	set_state(link_id, sm->name, sm->sym, &undefined);
}

static struct smatch_state *alloc_state(struct expression *expr, int is_true)
{
	struct smatch_state *state;

	state = __alloc_smatch_state(0);
	if (is_true)
		state->name = alloc_sname("true");
	else
		state->name = alloc_sname("false");
	state->data = expr;
	return state;
}

static int is_local_variable(struct expression *expr)
{
	struct symbol *sym;
	char *name;

	name = expr_to_var_sym(expr, &sym);
	free_string(name);
	if (!sym || !sym->scope || !sym->scope->token)
		return 0;
	if (cmp_pos(sym->scope->token->pos, cur_func_sym->pos) < 0)
		return 0;
	if (is_static(expr))
		return 0;
	return 1;
}

static int get_complication_score(struct expression *expr)
{
	int score = 0;

	expr = strip_expr(expr);

	switch (expr->type) {
	case EXPR_CALL:
		return 999;
	case EXPR_LOGICAL:
	case EXPR_BINOP:
		score += get_complication_score(expr->left);
		score += get_complication_score(expr->right);
		return score;
	case EXPR_SYMBOL:
		if (is_local_variable(expr))
			return 1;
		return 999;
	case EXPR_VALUE:
		return 0;
#if 0
	case EXPR_PREOP:
		if (expr->op == SPECIAL_INCREMENT ||
		    expr->op == SPECIAL_DECREMENT)
			return 999;
		return get_complication_score(expr->unop);
#endif
	default:
		return 999;
	}
}

static int condition_too_complicated(struct expression *expr)
{
	if (get_complication_score(expr) > 2)
		return 1;
	return 0;

}

static void store_all_links(struct expression *expr, const char *condition)
{
	char *var;
	struct symbol *sym;

	expr = strip_expr(expr);

	switch (expr->type) {
	case EXPR_BINOP:
		store_all_links(expr->left, condition);
		store_all_links(expr->right, condition);
		return;
	case EXPR_VALUE:
		return;
	}

	var = expr_to_var_sym(expr, &sym);
	if (!var || !sym)
		goto free;
	save_link_var_sym(var, sym, condition);
free:
	free_string(var);
}

static void match_condition(struct expression *expr)
{
	struct smatch_state *true_state, *false_state;
	char *name;

	if (condition_too_complicated(expr))
		return;

	name = expr_to_str(expr);
	if (!name)
		return;
	true_state = alloc_state(expr, TRUE);
	false_state = alloc_state(expr, FALSE);
	set_true_false_states(my_id, name, NULL, true_state, false_state);
	store_all_links(expr, alloc_sname(name));
	free_string(name);
}

struct smatch_state *get_stored_condition(struct expression *expr)
{
	struct smatch_state *state;
	char *name;

	name = expr_to_str(expr);
	if (!name)
		return NULL;

	state = get_state(my_id, name, NULL);
	free_string(name);
	return state;
}

void register_stored_conditions(int id)
{
	my_id = id;

	add_hook(&match_condition, CONDITION_HOOK);
}

void register_stored_conditions_links(int id)
{
	link_id = id;
	add_merge_hook(link_id, &merge_links);
	add_modification_hook(link_id, &match_modify);
}

