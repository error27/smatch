/*
 * sparse/check_debug.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"
#include "smatch_slist.h"
#include "smatch_extra.h"

static int my_id;

static void match_all_values(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;

	slist = get_all_states(SMATCH_EXTRA);
	__print_slist(slist);
	free_slist(&slist);
}

static void match_cur_slist(const char *fn, struct expression *expr, void *info)
{
	__print_cur_slist();
}

static void match_print_value(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;
	struct sm_state *tmp;
	struct expression *arg_expr;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	if (arg_expr->type != EXPR_STRING) {
		sm_msg("error:  the argument to %s is supposed to be a string literal", fn);
		return;
	}

	slist = get_all_states(SMATCH_EXTRA);
	FOR_EACH_PTR(slist, tmp) {
		if (!strcmp(tmp->name, arg_expr->string->data))
			sm_msg("%s = %s", tmp->name, tmp->state->name);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_print_implied(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	struct range_list *rl = NULL;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	get_implied_range_list(arg, &rl);

	name = get_variable_from_expr_complex(arg, NULL);
	sm_msg("implied: %s = '%s'", name, show_ranges_sval(rl));
	free_string(name);
}

static void match_print_implied_min(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (get_implied_min_sval(arg, &sval))
		sm_msg("implied min: %s = %s", name, sval_to_str(sval));
	else
		sm_msg("implied min: %s = <unknown>", name);

	free_string(name);
}

static void match_print_implied_max(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (get_implied_max_sval(arg, &sval))
		sm_msg("implied max: %s = %s", name, sval_to_str(sval));
	else
		sm_msg("implied max: %s = <unknown>", name);

	free_string(name);
}

static void match_print_hard_max(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (get_hard_max(arg, &sval))
		sm_msg("hard max: %s = %s", name, sval_to_str(sval));
	else
		sm_msg("hard max: %s = <unknown>", name);

	free_string(name);
}

static void match_print_fuzzy_max(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (get_fuzzy_max_sval(arg, &sval))
		sm_msg("fuzzy max: %s = %s", name, sval_to_str(sval));
	else
		sm_msg("fuzzy max: %s = <unknown>", name);

	free_string(name);
}

static void match_print_absolute_min(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (get_absolute_min_sval(arg, &sval))
		sm_msg("absolute min: %s = %s", name, sval_to_str(sval));
	else
		sm_msg("absolute min: %s = <unknown>", name);

	free_string(name);
}

static void match_print_absolute_max(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	get_absolute_max_sval(arg, &sval);

	name = get_variable_from_expr_complex(arg, NULL);
	sm_msg("absolute max: %s = %s", name, sval_to_str(sval));
	free_string(name);
}

static void match_sval_info(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	sval_t sval;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr_complex(arg, NULL);

	if (!get_implied_value(arg, &sval)) {
		sm_msg("no sval for '%s'", name);
		goto free;
	}

	sm_msg("implied: %s %c%d ->value = %llx", name, sval_unsigned(sval) ? 'u' : 's', sval_bits(sval), sval.value);
free:
	free_string(name);
}

static void print_possible(struct sm_state *sm)
{
	struct sm_state *tmp;

	sm_msg("Possible values for %s", sm->name);
	FOR_EACH_PTR(sm->possible, tmp) {
		printf("%s\n", tmp->state->name);
	} END_FOR_EACH_PTR(tmp);
	sm_msg("===");
}

static void match_possible(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;
	struct sm_state *tmp;
	struct expression *arg_expr;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	if (arg_expr->type != EXPR_STRING) {
		sm_msg("error:  the argument to %s is supposed to be a string literal", fn);
		return;
	}

	slist = get_all_states(SMATCH_EXTRA);
	FOR_EACH_PTR(slist, tmp) {
		if (!strcmp(tmp->name, arg_expr->string->data))
			print_possible(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_buf_size(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg;
	int elements, bytes;
	char *name;

	arg = get_argument_from_call_expr(expr->args, 0);
	elements = get_array_size(arg);
	bytes = get_array_size_bytes(arg);

	name = get_variable_from_expr_complex(arg, NULL);
	sm_msg("buf size: '%s' %d elements, %d bytes", name, elements, bytes);
	free_string(name);
}

static void match_note(const char *fn, struct expression *expr, void *info)
{
	struct expression *arg_expr;

	arg_expr = get_argument_from_call_expr(expr->args, 0);
	if (arg_expr->type != EXPR_STRING) {
		sm_msg("error:  the argument to %s is supposed to be a string literal", fn);
		return;
	}
	sm_msg("%s", arg_expr->string->data);
}

static void print_related(struct sm_state *sm)
{
	struct relation *rel;

	if (!estate_related(sm->state))
		return;

	sm_prefix();
	sm_printf("%s: ", sm->name);
	FOR_EACH_PTR(estate_related(sm->state), rel) {
		sm_printf("%s %s ", show_special(rel->op), rel->name);
	} END_FOR_EACH_PTR(rel);
	sm_printf("\n");
}

static void match_dump_related(const char *fn, struct expression *expr, void *info)
{
	struct state_list *slist;
	struct sm_state *tmp;

	slist = get_all_states(SMATCH_EXTRA);
	FOR_EACH_PTR(slist, tmp) {
		print_related(tmp);
	} END_FOR_EACH_PTR(tmp);
	free_slist(&slist);
}

static void match_debug_on(const char *fn, struct expression *expr, void *info)
{
	option_debug = 1;
}

static void match_debug_off(const char *fn, struct expression *expr, void *info)
{
	option_debug = 0;
}

void check_debug(int id)
{
	my_id = id;
	add_function_hook("__smatch_all_values", &match_all_values, NULL);
	add_function_hook("__smatch_value", &match_print_value, NULL);
	add_function_hook("__smatch_implied", &match_print_implied, NULL);
	add_function_hook("__smatch_implied_min", &match_print_implied_min, NULL);
	add_function_hook("__smatch_implied_max", &match_print_implied_max, NULL);
	add_function_hook("__smatch_hard_max", &match_print_hard_max, NULL);
	add_function_hook("__smatch_fuzzy_max", &match_print_fuzzy_max, NULL);
	add_function_hook("__smatch_absolute_min", &match_print_absolute_min, NULL);
	add_function_hook("__smatch_absolute_max", &match_print_absolute_max, NULL);
	add_function_hook("__smatch_sval_info", &match_sval_info, NULL);
	add_function_hook("__smatch_possible", &match_possible, NULL);
	add_function_hook("__smatch_cur_slist", &match_cur_slist, NULL);
	add_function_hook("__smatch_buf_size", &match_buf_size, NULL);
	add_function_hook("__smatch_note", &match_note, NULL);
	add_function_hook("__smatch_dump_related", &match_dump_related, NULL);
	add_function_hook("__smatch_debug_on", &match_debug_on, NULL);
	add_function_hook("__smatch_debug_off", &match_debug_off, NULL);
}
