/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Linus Torvalds, all rights reserved.
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

unsigned int pre_buffer_size = 0;
unsigned char pre_buffer[8192];

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

static void handle_switch(char *arg)
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
		return;
	}

	case 'I':
		add_pre_buffer("#add_include \"%s/\"\n", arg+1);
		return;
	default:
		fprintf(stderr, "unknown switch '%s'\n", arg);
	}	
}

static void clean_up_statement(struct statement *stmt, void *_parent, int flags);
static void clean_up_symbol(struct symbol *sym, void *_parent, int flags);

static void simplify_statement(struct statement *stmt, struct symbol *fn)
{
	if (!stmt)
		return;
	switch (stmt->type) {
	case STMT_RETURN:
	case STMT_EXPRESSION:
		evaluate_expression(stmt->expression);
		return;
	case STMT_COMPOUND:
		symbol_iterate(stmt->syms, clean_up_symbol, fn);
		statement_iterate(stmt->stmts, clean_up_statement, fn);
		return;
	case STMT_IF:
		evaluate_expression(stmt->if_conditional);
		simplify_statement(stmt->if_true, fn);
		simplify_statement(stmt->if_false, fn);
		return;
	case STMT_ITERATOR:
		evaluate_expression(stmt->iterator_pre_condition);
		evaluate_expression(stmt->iterator_post_condition);
		simplify_statement(stmt->iterator_pre_statement, fn);
		simplify_statement(stmt->iterator_statement, fn);
		simplify_statement(stmt->iterator_post_statement, fn);
		return;
	case STMT_SWITCH:
		evaluate_expression(stmt->switch_expression);
		simplify_statement(stmt->switch_statement, fn);
		return;
	case STMT_CASE:
		evaluate_expression(stmt->case_expression);
		evaluate_expression(stmt->case_to);
		simplify_statement(stmt->case_statement, fn);
		return;
	default:
		break;
	}
}

static void clean_up_statement(struct statement *stmt, void *_parent, int flags)
{
	struct symbol *parent = _parent;
	simplify_statement(stmt, parent);
}

static void clean_up_symbol(struct symbol *sym, void *_parent, int flags)
{
	struct symbol *parent = _parent;
	struct symbol *type;

	examine_symbol_type(sym);
	if (sym->initializer)
		evaluate_initializer(sym, sym->initializer);
	type = sym->ctype.base_type;
	if (type && type->type == SYM_FN) {
		symbol_iterate(type->arguments, clean_up_symbol, parent);
		if (type->stmt)
			simplify_statement(type->stmt, sym);
	}
}

int main(int argc, char **argv)
{
	int i, fd;
	char *filename = NULL;
	struct token *token;
	struct symbol_list *list = NULL;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	// Stupid defines to make various headers happy
	add_pre_buffer("#define __GNUC__ 2\n");
	add_pre_buffer("#define __GNUC_MINOR__ 95\n");

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (arg[0] == '-') {
			handle_switch(arg+1);
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

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	translation_unit(token, &list);

	// Do type evaluation and simplify
	symbol_iterate(list, clean_up_symbol, NULL);

#if 1
	// Show the end result.
	show_symbol_list(list, "\n\n");
	printf("\n\n");
#endif

	// And show the allocation statistics
	show_ident_alloc();
	show_token_alloc();
	show_symbol_alloc();
	show_expression_alloc();
	show_statement_alloc();
	show_string_alloc();
	show_bytes_alloc();
	return 0;
}
