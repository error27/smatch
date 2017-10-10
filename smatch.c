/*
 * Copyright (C) 2006 Dan Carpenter.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/copyleft/gpl.txt
 */

#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include "smatch.h"
#include "check_list.h"

char *option_debug_check = (char *)"";
char *option_project_str = (char *)"smatch_generic";
enum project_type option_project = PROJ_NONE;
char *data_dir;
int option_no_data = 0;
int option_spammy = 0;
int option_info = 0;
int option_full_path = 0;
int option_param_mapper = 0;
int option_call_tree = 0;
int option_no_db = 0;
int option_enable = 0;
int option_debug_related;
int option_file_output;
int option_time;
char *option_datadir_str;
FILE *sm_outfd;
FILE *sql_outfd;
FILE *caller_info_fd;

typedef void (*reg_func) (int id);
#define CK(_x) {.name = #_x, .func = &_x, .enabled = 0},
static struct reg_func_info {
	const char *name;
	reg_func func;
	int enabled;
} reg_funcs[] = {
	{NULL, NULL},
#include "check_list.h"
};
#undef CK
int num_checks = ARRAY_SIZE(reg_funcs) - 1;

const char *check_name(unsigned short id)
{
	if (id >= ARRAY_SIZE(reg_funcs))
		return "internal";

	return reg_funcs[id].name;
}

int id_from_name(const char *name)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(reg_funcs); i++) {
		if (!strcmp(name, reg_funcs[i].name))
			return i;
	}
	return 0;
}

static void show_checks(void)
{
	int i;

	for (i = 1; i < ARRAY_SIZE(reg_funcs); i++) {
		if (!strncmp(reg_funcs[i].name, "check_", 6))
			printf("%3d. %s\n", i, reg_funcs[i].name);
	}
	exit(1);
}
static void enable_check(int i)
{
	if (1 <= i && i < ARRAY_SIZE(reg_funcs))
		reg_funcs[i].enabled = 1;
}

static void enable_checks(const char *s)
{
	int n = 0, lo = -1, i;

	do {
		switch (*s) {
		case ',':
		case '\0':
			if (lo < 0)
				enable_check(n);
			else
				for (i = lo; i <= n; ++i)
					enable_check(i);
			lo = -1;
			n = 0;
			break;
		case '-':
			lo = n;
			n = 0;
			break;
		case '0' ... '9':
			n = 10*n + (*s - '0');
			break;
		default:
			fprintf(stderr, "invalid character '%c'\n", *s);
			exit(1);
		}
	} while (*s++);
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
	printf("--data=<dir>: overwrite path to default smatch data directory.\n");
	printf("--full-path:  print the full pathname.\n");
	printf("--debug-implied:  print debug output about implications.\n");
	printf("--assume-loops:  assume loops always go through at least once.\n");
	printf("--two-passes:  use a two pass system for each function.\n");
	printf("--file-output:  instead of printing stdout, print to \"file.c.smatch_out\".\n");
	printf("--help:  print this helpful message.\n");
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
	while (*argcp >= 2) {
		int found = 0;
		if (!strcmp((*argvp)[1], "--help"))
			help();

		if (!strcmp((*argvp)[1], "--show-checks"))
			show_checks();

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
		if (!found && !strncmp((*argvp)[1], "--data=", 7)) {
			option_datadir_str = (*argvp)[1] + 7;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}
		if (!found && !strncmp((*argvp)[1], "--debug=", 8)) {
			option_debug_check = (*argvp)[1] + 8;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}
		if (!found && strncmp((*argvp)[1], "--trace=", 8) == 0) {
			trace_variable = (*argvp)[1] + 8;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}
		if (!found && strncmp((*argvp)[1], "--enable=", 9) == 0) {
			enable_checks((*argvp)[1] + 9);
			option_enable = 1;
			(*argvp)[1] = (*argvp)[0];
			found = 1;
		}

		OPTION(spammy);
		OPTION(info);
		OPTION(debug);
		OPTION(debug_implied);
		OPTION(debug_related);
		OPTION(assume_loops);
		OPTION(no_data);
		OPTION(two_passes);
		OPTION(full_path);
		OPTION(param_mapper);
		OPTION(call_tree);
		OPTION(file_output);
		OPTION(time);
		OPTION(no_db);
		if (!found)
			break;
		(*argcp)--;
		(*argvp)++;
	}

	if (strcmp(option_project_str, "smatch_generic") != 0)
		option_project = PROJ_UNKNOWN;
	if (strcmp(option_project_str, "kernel") == 0)
		option_project = PROJ_KERNEL;
	if (strcmp(option_project_str, "wine") == 0)
		option_project = PROJ_WINE;
}

static char *get_data_dir(char *arg0)
{
	char *bin_dir;
	char *orig;
	char buf[256];
	char *dir;

	if (option_no_data)
		return NULL;

	if (option_datadir_str) {
		if (access(option_datadir_str, R_OK))
			printf("Warning: %s is not accessible -- ignore.\n",
					option_datadir_str);
		else
			return alloc_string(option_datadir_str);
	}

	strncpy(buf, "smatch_data/", sizeof(buf));
	dir = alloc_string(buf);
	if (!access(dir, R_OK))
		return dir;

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
	printf("Use --no-data or --data to suppress this message.\n");
	return NULL;
}

int main(int argc, char **argv)
{
	int i;
	reg_func func;

	sm_outfd = stdout;
	sql_outfd = stdout;
	caller_info_fd = stdout;
	parse_args(&argc, &argv);

	/* this gets set back to zero when we parse the first function */
	final_pass = 1;

	data_dir = get_data_dir(argv[0]);

	allocate_hook_memory();
	create_function_hook_hash();
	open_smatch_db();
	for (i = 1; i < ARRAY_SIZE(reg_funcs); i++) {
		func = reg_funcs[i].func;
		/* The script IDs start at 1.
		   0 is used for internal stuff. */
		if (!option_enable || reg_funcs[i].enabled || !strncmp(reg_funcs[i].name, "register_", 9))
			func(i);
	}

	smatch(argc, argv);
	free_string(data_dir);
	return 0;
}
