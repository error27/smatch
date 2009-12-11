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

char *option_project_str = (char *)"";
enum project_type option_project = PROJ_NONE;
char *data_dir;
int option_no_data = 0;
int option_spammy = 0;
int option_full_path = 0;

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
void check_deref_check(int id);
void check_hold_dev(int id);
void check_redundant_null_check(int id);
void check_signed(int id);
void check_precedence(int id);
/* <- your test goes here */

/* may as well put wine scripts all together */
void check_wine(int id);
void check_wine_filehandles(int id);
void check_wine_WtoA(int id);

/* void register_template(int id); */

#define CK(_x) {.name = #_x, .func = &_x}
static struct reg_func_info {
	const char *name;
	reg_func func;
} reg_funcs[] = {
	CK(register_smatch_extra), /* smatch_extra always has to be first */
	CK(register_smatch_ignore),
	CK(check_debug),
	CK(check_assigned_expr),

	CK(check_null_deref),
	CK(check_overflow),
	CK(check_memory),
	CK(check_type),
	CK(check_allocation_funcs),
	CK(check_leaks),
	CK(check_frees_argument),
	CK(check_balanced),
	CK(check_deref_check),
	CK(check_redundant_null_check),
	CK(check_signed),
	CK(check_precedence),

	/* <- your test goes here */
	/* CK(register_template), */

	/* kernel specific */
	CK(check_locking),
	CK(check_puts_argument),
	CK(check_err_ptr),
	CK(check_err_ptr_deref),
	CK(check_hold_dev),

	/* wine specific stuff */
	CK(check_wine),
	CK(check_wine_filehandles),
	CK(check_wine_WtoA),

	CK(register_modification_hooks),
	CK(register_containers),
	CK(register_implications), /* implications always has to be last */
};

const char *check_name(unsigned short id)
{
	if (id > ARRAY_SIZE(reg_funcs)) {
		return "internal";
	}
	return reg_funcs[id - 1].name;
}

int id_from_name(const char *name)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(reg_funcs); i++){
		if (!strcmp(name, reg_funcs[i].name))
			return i + 1;
	}
	return 0;
}

struct smatch_state *default_state[ARRAY_SIZE(reg_funcs)];

static void help(void)
{
	printf("Usage:  smatch [smatch arguments][sparse arguments] file.c\n");
	printf("--project=<name> or -p=<name>: project specific tests\n");
	printf("--spammy:  print superfluous crap.\n");
	printf("--debug:  print lots of debug output.\n");
	printf("--no-data:  do not use the /smatch_data/ directory.\n");
	printf("--full-path:  print the full pathname.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--oom <num>:  number of mB memory to use before giving up.\n");
	printf("--no-implied:  ignore implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	printf("--known-conditions:  don't branch for known conditions.\n");
	printf("--two-passes:  use a two pass system for each function.\n");
	printf("--help:  print this helpfull message.\n");
	exit(1);
}

void parse_args(int *argcp, char ***argvp)
{
	while(*argcp >= 2) {
		if (!strncmp((*argvp)[1], "--project=", 10)) {
			option_project_str = (*argvp)[1] + 10;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strncmp((*argvp)[1], "-p=", 3)) {
			option_project_str = (*argvp)[1] + 3;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--debug")) {
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
		} else if (!strcmp((*argvp)[1], "--two-passes")) {
			option_two_passes = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--full-path")) {
			option_full_path = 1;
			(*argvp)[1] = (*argvp)[0];
		} else if (!strcmp((*argvp)[1], "--help")) {
			help();
		} else {
			break;
		}
		(*argcp)--;
		(*argvp)++;
	}

	if (!strcmp(option_project_str, "kernel"))
		option_project = PROJ_KERNEL;
	if (!strcmp(option_project_str, "wine"))
		option_project = PROJ_WINE;
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

	create_function_hash();
	for(i = 0; i < ARRAY_SIZE(reg_funcs); i++){
		func = reg_funcs[i].func;
		/* The script IDs start at 1.
		   0 is used for internal stuff. */
		func(i + 1);
	}
	register_function_hooks(-1);

	smatch(argc, argv);
	free_string(data_dir);
	return 0;
}
