/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *
 *  Licensed under the Open Software License version 1.1
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

#include "lib.h"
#include "token.h"
#include "parse.h"
#include "symbol.h"
#include "expression.h"

static unsigned int pre_buffer_size = 0;
static unsigned char pre_buffer[8192];

static char *include = NULL;
static int include_fd = -1;

static void add_pre_buffer(const char *fmt, ...)
{
	va_list args;
	unsigned int size;

	va_start(args, fmt);
	size = pre_buffer_size;
	size += vsnprintf(pre_buffer + size,
		sizeof(pre_buffer) - size,
		fmt, args);
	pre_buffer_size = size;
	va_end(args);
}

static char ** handle_switch(char *arg, char **next)
{
	switch (*arg) {
	case 'D': {
		const char *name = arg+1;
		const char *value = "";
		for (;;) {
			char c;
			c = *++arg;
			if (!c)
				break;
			if (isspace(c) || c == '=') {
				*arg = '\0';
				value = arg+1;
				break;
			}
		}
		add_pre_buffer("#define %s %s\n", name, value);
		return next;
	}

	case 'I':
		add_pre_buffer("#add_include \"%s/\"\n", arg+1);
		return next;
	case 'i':
		if (*next && !strcmp(arg, "include")) {
			char *name = *++next;
			int fd = open(name, O_RDONLY);
			include_fd = fd;
			include = name;
			if (fd < 0)
				perror(name);
			return next;
		}
	/* Fallthrough */
	default:
		/* Ignore unknown command line options - they're probably gcc switches */
		break;
	}
	return next;
}

static void clean_up_symbol(struct symbol *sym, void *_parent, int flags)
{
	check_duplicates(sym);
	evaluate_symbol(sym);
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = NULL, **args;
	struct token *token;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	add_pre_buffer("#define __linux__ 1\n");
	add_pre_buffer("#define __CHECKER__ 1\n");
	add_pre_buffer("#define cond_syscall(x)\n");
	add_pre_buffer("#nostdinc\n");
	add_pre_buffer("#add_include \"/home/torvalds/v2.5/linux/include/\"\n");
	add_pre_buffer("#add_include \"/home/torvalds/v2.5/linux/include/asm-i386/mach-default/\"\n");
	add_pre_buffer("#add_include \"/home/torvalds/v2.5/linux/arch/i386/mach-default/\"\n");
	add_pre_buffer("#add_include \"\"\n");
	add_pre_buffer("#define __KERNEL__\n");
	add_pre_buffer("#define __GNUC__ 2\n");
	add_pre_buffer("#define __GNUC_MINOR__ 95\n");
	add_pre_buffer("#define __builtin_constant_p(x) 0\n");
	add_pre_buffer("#define __func__ \"function\"\n");
	add_pre_buffer("#define __extension__\n");

	args = argv;
	for (;;) {
		char *arg = *++args;
		if (!arg)
			break;
		if (arg[0] == '-') {
			args = handle_switch(arg+1, args);
			continue;
		}
		filename = arg;
	}
		

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", argv[1]);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL);
	close(fd);

	// Prepend any "include" file to the stream.
	if (include_fd >= 0)
		token = tokenize(include, include_fd, token);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	translation_unit(token, &used_list);

	// Do type evaluation and simplify
	symbol_iterate(used_list, clean_up_symbol, NULL);
	return 0;
}
