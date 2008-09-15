/*
 * sparse/smatch.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 *  Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include "smatch.h"

typedef void (*reg_func) (int id);
void register_derefed_params(int id);
void register_null_deref(int id);
void register_smatch_extra(int id);
void register_overflow(int id);

const reg_func reg_funcs[] = {
	&register_smatch_extra,
	&register_derefed_params,
	&register_null_deref,
	&register_overflow,
	NULL
};

int main(int argc, char **argv)
{
	int i;
	reg_func func;
	
	/* The script IDs start at 1.
	   0 is used for internal stuff. */
	for(i = 1; (func = reg_funcs[i]); i++){
		func(i);
	}
	
	if (argc >= 2 && !strcmp(argv[1], "--debug")) {
		debug_states = 1;
		argc--;
		argv++;
	}
		
    	smatch(argc, argv);
	return 0;
}
