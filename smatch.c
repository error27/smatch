/*
 * sparse/smatch.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include <libgen.h>
#include "smatch.h"

char *bin_dir;

typedef void (*reg_func) (int id);
void register_smatch_extra(int id);
void register_smatch_ignore(int id);
void register_implications(int id);
void register_function_hooks(int id);
void check_null_deref(int id);
void check_overflow(int id);
void check_locking(int id);
void check_memory(int id);
void check_frees_argument(int id);
void check_leaks(int id);
void check_allocation_funcs(int id);
/* <- your test goes here */
/* void register_template(int id); */

static const reg_func reg_funcs[] = {
	&register_smatch_extra, /* smatch_extra always has to be first */
	&register_smatch_ignore,
	&check_null_deref,
	&check_overflow,
	&check_locking,
	&check_memory,
	// &check_allocation_funcs,
	// &check_leaks,
	// &check_frees_argument,

	/* <- your test goes here */
	/* &register_template, */

	&register_implications, /* implications always has to be last */
	NULL
};

static void help(void)
{
	printf("Usage:  smatch [smatch arguments][sparse arguments] file.c\n");
	printf("--debug:  print lots of debug output.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	printf("--known-conditions:  don't branch for known conditions.");
	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	reg_func func;

	bin_dir = dirname(alloc_string(argv[0]));

	/* The script IDs start at 1.
	   0 is used for internal stuff. */
	create_function_hash();
	for(i = 0; (func = reg_funcs[i]); i++){
		func(i + 1);
	}
	register_function_hooks(-1);
	
	while(argc >= 2) {
		if (!strcmp(argv[1], "--debug")) {
			debug_states = 1;
		} else if (!strcmp(argv[1], "--debug-implied")) {
			debug_implied_states = 1;
		} else if (!strcmp(argv[1], "--no-implied")) {
			option_no_implied = 1;
		} else if (!strcmp(argv[1], "--assume-loops")) {
			option_assume_loops = 1;
		} else if (!strcmp(argv[1], "--known-conditions")) {
			option_known_conditions = 1;
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
