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

static void handle_switch(char *arg)
{
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
