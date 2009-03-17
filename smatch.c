/*
 * sparse/smatch.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include "smatch.h"

typedef void (*reg_func) (int id);
void register_derefed_params(int id);
void register_null_deref(int id);
void register_smatch_extra(int id);
void register_smatch_ignore(int id);
void register_overflow(int id);
void register_locking(int id);
void register_memory(int id);
void register_implications(int id);
void register_frees_argument(int id);
/* <- your test goes here */
/* void register_template(int id); */

const reg_func reg_funcs[] = {
	&register_smatch_extra, /* smatch_extra always has to be first */
	&register_smatch_ignore,
	&register_null_deref,
	&register_overflow,
	&register_locking,
	&register_memory,
	// &register_frees_argument,

	/* <- your test goes here */
	/* &register_template, */

	&register_implications, /* implications always has to be last */
	NULL
};

void help()
{
	printf("Usage:  smatch [smatch arguments][sparse arguments] file.c\n");
	printf("--debug:  print lots of debug output.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	reg_func func;
	
	/* The script IDs start at 1.
	   0 is used for internal stuff. */
	for(i = 0; (func = reg_funcs[i]); i++){
		func(i + 1);
	}
	
	while(argc >= 2) {
		if (!strcmp(argv[1], "--debug")) {
			debug_states = 1;
		} else if (!strcmp(argv[1], "--debug-implied")) {
			debug_implied_states = 1;
		} else if (!strcmp(argv[1], "--no-implied")) {
			option_no_implied = 1;
		} else if (!strcmp(argv[1], "--assume-loops")) {
			option_assume_loops = 1;
		} else if (!strcmp(argv[1], "--help")) {
			help();
		} else {
			break;
		}
		argc--;
		argv++;
	}
	
    	smatch(argc, argv);
	return 0;
}
