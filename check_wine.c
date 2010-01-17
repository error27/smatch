/*
 * sparse/check_wine.c
 *
 * Copyright (C) 2009 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include "smatch.h"

void check_wine(int id)
{
	/* Really I should figure out how to get the no return
	   attribute from sparse, but this is a quick hack. */

	if (option_project != PROJ_WINE)
		return;
	add_function_hook("fatal", &__match_nullify_path_hook, NULL);
	add_function_hook("fatal_error", &__match_nullify_path_hook, NULL);
	add_function_hook("__assert_fail", &__match_nullify_path_hook, NULL);
	add_function_hook("__assert_perror_fail", &__match_nullify_path_hook, NULL);
	add_function_hook("raise_status", &__match_nullify_path_hook, NULL);
	add_function_hook("RaiseException", &__match_nullify_path_hook, NULL);
	add_function_hook("RpcRaiseException", &__match_nullify_path_hook, NULL);
	add_function_hook("RtlRaiseException", &__match_nullify_path_hook, NULL);
	add_function_hook("pp_internal_error", &__match_nullify_path_hook, NULL);
	add_function_hook("ExitThread", &__match_nullify_path_hook, NULL);
	add_function_hook("ExitProcess", &__match_nullify_path_hook, NULL);
	add_function_hook("exit", &__match_nullify_path_hook, NULL);

}
