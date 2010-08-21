
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
#include "check_list.h"

char *option_project_str = (char *)"";
enum project_type option_project = PROJ_NONE;
char *data_dir;
int option_no_data = 0;
int option_spammy = 0;
int option_info = 0;
int option_full_path = 0;
int option_param_mapper = 0;
int option_print_returns = 0;
int option_call_tree = 0;

typedef void (*reg_func) (int id);
#define CK(_x) {.name = #_x, .func = &_x},
static struct reg_func_info {
	const char *name;
	reg_func func;
} reg_funcs[] = {
#include "check_list.h"
};
#undef CK
int num_checks = ARRAY_SIZE(reg_funcs);

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

	for (i = 0; i < ARRAY_SIZE(reg_funcs); i++) {
		if (!strcmp(name, reg_funcs[i].name))
			return i + 1;
	}
	return 0;
}

static void help(void)
{
	printf("Usage:  smatch [smatch arguments][sparse arguments] file.c\n");
	printf("--project=<name> or -p=<name>: project specific tests\n");
	printf("--spammy:  print superfluous crap.\n");
	printf("--info:  print info used to fill smatch_data/.\n");
	printf("--debug:  print lots of debug output.\n");
	printf("--param-mapper:  enable param_mapper output.\n");
	printf("--no-data:  do not use the /smatch_data/ directory.\n");
	printf("--full-path:  print the full pathname.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--no-implied:  ignore implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	printf("--known-conditions:  don't branch for known conditions.\n");
	printf("--two-passes:  use a two pass system for each function.\n");
	printf("--help:  print this helpfull message.\n");
	exit(1);
}

static int match_option(const char *arg, const char *option)
{
	char *str;
	char *tmp;
	int ret = 0;

	str = malloc(strlen(option) + 3); 
	snprintf(str, strlen(option) + 3, "--%s", option);
	tmp = str;
	while (*tmp) {
		if (*tmp == '_')
			*tmp = '-';
		tmp++;
	}
	if (!strcmp(arg, str))
		ret = 1;
	free(str);
	return ret;
} 

#define OPTION(_x) do {					\
	if (!found && match_option((*argvp)[1], #_x)) { \
		found = 1;				\
		option_##_x = 1;					\
		(*argvp)[1] = (*argvp)[0];		\
	}                                               \
} while (0)

void parse_args(int *argcp, char ***argvp)
{
	while(*argcp >= 2) {
		int found = 0;
		if (!strcmp((*argvp)[1], "--help")) {
			help();
		}
		if (!found && !strncmp((*argvp)[1], "--project=", 10)) {
			option_project_str = (*argvp)[1] + 10;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}
		if (!found && !strncmp((*argvp)[1], "-p=", 3)) {
			option_project_str = (*argvp)[1] + 3;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}
		OPTION(spammy);
		OPTION(info);
		OPTION(debug);
		OPTION(debug_implied);
		OPTION(no_implied);
		OPTION(assume_loops);
		OPTION(known_conditions);
		OPTION(no_data);
		OPTION(two_passes);
		OPTION(full_path);
		OPTION(param_mapper);
		OPTION(print_returns);
		OPTION(call_tree);
		if (!found)
			break;
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
	char *orig;
	char buf[256];
	char *dir;

	if (option_no_data) {
		return NULL;
	}

	orig = alloc_string(arg0);
	bin_dir = dirname(orig);
	strncpy(buf, bin_dir, 254);
	free_string(orig);

	buf[255] = '\0';
	strncat(buf, "/smatch_data/", 254 - strlen(buf));
	dir = alloc_string(buf);
	if (!access(dir, R_OK))
		return dir;
	free_string(dir);
	snprintf(buf, 254, "%s/smatch_data/", SMATCHDATADIR);
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

	create_function_hook_hash();
	for (i = 0; i < ARRAY_SIZE(reg_funcs); i++) {
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
