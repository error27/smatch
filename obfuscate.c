/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp, all rights reserved.
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

static void emit_blob(struct symbol *sym)
{
	int bit_size = sym->bit_size;

	if (bit_size & 7)
		warn(sym->pos, "emitting symbol of size %d\n", bit_size);
	printf("unsigned char %s[%d]\n", show_ident(sym->ident), bit_size >> 3);
}

void emit_symbol(struct symbol *sym, void *_parent, int flags)
{
	struct symbol *ctype;

	evaluate_symbol(sym);
	ctype = sym->ctype.base_type;
	if (!ctype)
		return;
	switch (ctype->type) {
	case SYM_PTR:
	case SYM_ARRAY:
	case SYM_STRUCT:
	case SYM_UNION:
	case SYM_BASETYPE:
		emit_blob(sym);
		return;
	default:
		return;
	}
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = argv[1];
	struct token *token;
	struct symbol_list *list = NULL;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

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
	symbol_iterate(list, emit_symbol, NULL);

	return 0;
}
