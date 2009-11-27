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
	add_function_hook("RpcRaiseException", &__match_nullify_path_hook, NULL);
}
