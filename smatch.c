/*
 * sparse/smatch.c
 *
 * Copyright (C) 2006 Dan Carpenter.
 *
 * Licensed under the Open Software License version 1.1
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include "smatch.h"

char *data_dir;
int option_no_data = 0;
int option_spammy = 0;

typedef void (*reg_func) (int id);
void register_smatch_extra(int id);
void register_smatch_ignore(int id);
void register_implications(int id);
void register_function_hooks(int id);
void register_modification_hooks(int id);
void register_containers(int id);
void check_debug(int id);
void check_assigned_expr(int id);
void check_null_deref(int id);
void check_overflow(int id);
void check_locking(int id);
void check_memory(int id);
void check_frees_argument(int id);
void check_puts_argument(int id);
void check_leaks(int id);
void check_type(int id);
void check_allocation_funcs(int id);
void check_err_ptr(int id);
void check_err_ptr_deref(int id);
void check_balanced(int id);
void check_initializer_deref(int id);
void check_deref_check(int id);
/* <- your test goes here */
/* void register_template(int id); */

static const reg_func reg_funcs[] = {
	&register_smatch_extra, /* smatch_extra always has to be first */
	&register_smatch_ignore,
	&check_debug,
	&check_assigned_expr,
	&check_null_deref,
	&check_overflow,
	&check_locking,
	&check_memory,
	&check_type,
	&check_allocation_funcs,
	&check_leaks,
	&check_frees_argument,
	&check_puts_argument,
	&check_err_ptr,
	&check_err_ptr_deref,
	&check_balanced,
	&check_initializer_deref,
	&check_deref_check,

	/* <- your test goes here */
	/* &register_template, */

	&register_modification_hooks,
	&register_containers,
	&register_implications, /* implications always has to be last */
	NULL
};

struct smatch_state *default_state[sizeof(reg_funcs)/sizeof(reg_func)];

static void help(void)
{
	printf("Usage:  smatch [smatch arguments][sparse arguments] file.c\n");
	printf("--debug:  print lots of debug output.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--oom <num>:  number of mB memory to use before giving up.\n");
	printf("--no-implied:  ignore implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	printf("--known-conditions:  don't branch for known conditions.\n");
	printf("--no-data:  do not use the /smatch_data/ directory.\n");
	printf("--spammy:  print superfluous crap.\n");
	printf("--help:  print this helpfull message.\n");
	exit(1);
}

void parse_args(int *argcp, char ***argvp)
{
	while(*argcp >= 2) {
		if (!strcmp((*argvp)[1], "--debug")) {
			debug_states = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--debug-implied")) {
			debug_implied_states = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--oom")) {
			option_oom_kb = atoi((*argvp)[2]) * 1000;
			(*argvp)[2] = (*argvp)[0];
			(*argcp)--;
			(*argvp)++;
		} else if (!strcmp((*argvp)[1], "--no-implied")) {
			option_no_implied = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--assume-loops")) {
			option_assume_loops = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--known-conditions")) {
			option_known_conditions = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--no-data")) {
			option_no_data = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--spammy")) {
			option_spammy = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--help")) {
			help();
		} else {
			break;
		}
		(*argcp)--;
		(*argvp)++;
	}
}

static char *get_data_dir(char *arg0)
{
	char *bin_dir;
	char buf[256];
	char *dir;

	if (option_no_data) {
		return NULL;
	}
	bin_dir = dirname(alloc_string(arg0));
	strncpy(buf, bin_dir, 254);
	buf[255] = '\0';
	strncat(buf, "/smatch_data/", 254 - strlen(buf));
	dir = alloc_string(buf);
	if (!access(dir, R_OK))
		return dir;
	printf("Warning: %s is not accessible.\n", dir);
	printf("Use --no-data to suppress this message.\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	reg_func func;

	parse_args(&argc, &argv);

	data_dir = get_data_dir(argv[0]);

	/* The script IDs start at 1.
	   0 is used for internal stuff. */
	create_function_hash();
	for(i = 0; (func = reg_funcs[i]); i++){
		func(i + 1);
	}
	register_function_hooks(-1);

	smatch(argc, argv);
	free_string(data_dir);
	return 0;
}
