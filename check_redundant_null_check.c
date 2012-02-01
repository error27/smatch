/*
 * sparse/check_redundant_null_check.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

/*
 * Some functions are designed to handle NULL values but people always
 * do stuff like:  if (foo) kfree(foo);
 * The if check is unneeded and a waste of precious CPU cycles.  Also a 
 * little bit messy.
 *
 * List of wine functions taken from Michael Stefaniuc's 
 * redundant_null_check.pl
 *
 * This test is not the prettiest.  Smatch has a feature where it 
 * simplifies comparisons to zero and I wanted to take advantage of 
 * that, but that also makes things a bit complicated.
 *
 * We work backwards.  When we hit a kfree() we ask was the last statement
 * an if statement, and was the last comparison checking the pointer we
 * are about to free?  If all three answers are yes, then print a message.
 *
 */

#include "smatch.h"

static int my_id;

static struct statement *this_statement = NULL;
static struct statement *previous_statement = NULL;
static struct expression *previous_condition = NULL;

static const char *wine_funcs[] = {
	"HeapFree",
	"RtlFreeHeap",
	"SysFreeString",
	"CryptMemFree",
	"DllFreeSplMem",
	"FreeEnvironmentStringsA",
	"Free",
	"GdipFree",
	"I_RpcFree",
	"ldap_memfreeA",
	"ldap_memfreeW",
	"LsaFreeMemory",
	"MyFree",
	"SetupTermDefaultQueueCallback",
};

static void dont_check(const char *fn, struct expression *expr, void *unused)
{
	struct expression *arg;
	char *name = NULL;
	char *condition_name = NULL;

	if (!previous_statement || !previous_condition || previous_statement->type != STMT_IF)
		return;

	arg = get_argument_from_call_expr(expr->args, 0);
	name = get_variable_from_expr(arg, NULL);
	if (!name)
		goto free;
	condition_name = get_variable_from_expr(previous_condition, NULL);
	if (!condition_name)
		goto free;
	if (!strcmp(name, condition_name))
		sm_msg("info: redundant null check on %s calling %s()", name, fn);

free:
	free_string(name);
	free_string(condition_name);
}

static void match_statement(struct statement *stmt)
{
	if (stmt->type == STMT_COMPOUND)
		return;
	previous_statement = this_statement;
	this_statement = stmt;
}

static void match_condition(struct expression *expr)
{
	previous_condition = expr;
}

void check_redundant_null_check(int id)
{
	my_id = id;
	int i;

	add_hook(&match_statement, STMT_HOOK);
	add_hook(&match_condition, CONDITION_HOOK);
	add_function_hook("free", &dont_check, NULL);
	if (option_project == PROJ_KERNEL) {
		add_function_hook("kfree", &dont_check, NULL);
	} else if (option_project == PROJ_WINE) {
		for (i = 0; i < ARRAY_SIZE(wine_funcs); i++) {
			add_function_hook(wine_funcs[i], &dont_check, NULL);
		}
	}
}
