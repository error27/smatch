/*
 * smatch/smatch_data_source.c
 *
 * Copyright (C) 2013 Oracle.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"

static int my_id;

static char *get_source_parameter(struct expression *expr)
{
	struct symbol *sym;
	char *name;
	int param;
	struct sm_state *orig, *cur;
	char *ret = NULL;
	char buf[32];

	name = expr_to_var_sym(expr, &sym);
	if (!name || !sym)
		goto out;
	param = get_param_num_from_sym(sym);
	if (param < 0)
		goto out;
	cur = get_sm_state(SMATCH_EXTRA, name, sym);
	if (!cur)
		goto out;
	orig = get_sm_state_slist(get_start_states(), SMATCH_EXTRA, name, sym);
	if (!orig)
		goto out;
	if (orig != cur)
		goto out;

	snprintf(buf, sizeof(buf), "p %d", param);
	ret = alloc_string(buf);

out:
	free_string(name);
	return ret;
}

static char *get_source_assignment(struct expression *expr)
{
	struct expression *right;
	char *name;
	char buf[64];
	char *ret;

	right = get_assigned_expr(expr);
	right = strip_expr(right);
	if (!right)
		return NULL;
	if (right->type != EXPR_CALL || right->fn->type != EXPR_SYMBOL)
		return NULL;
	name = expr_to_str(right->fn);
	if (!name)
		return NULL;
	snprintf(buf, sizeof(buf), "r %s", name);
	ret = alloc_string(buf);
	free_string(name);
	return ret;
}

static char *get_source_str(struct expression *expr)
{
	char *source;

	source = get_source_parameter(expr);
	if (source)
		return source;
	return get_source_assignment(expr);
}

static void match_caller_info(struct expression *expr)
{
	struct expression *tmp;
	char *source;
	int i;

	i = -1;
	FOR_EACH_PTR(expr->args, tmp) {
		i++;
		source = get_source_str(tmp);
		if (!source)
			continue;
		sql_insert_caller_info(expr, DATA_SOURCE, i, "$$", source);
		free_string(source);
	} END_FOR_EACH_PTR(tmp);
}

void register_data_source(int id)
{
	if (!option_info)
		return;
	my_id = id;
	add_hook(&match_caller_info, FUNCTION_CALL_HOOK);
}
