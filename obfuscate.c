/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003-2004 Linus Torvalds
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

static void emit_blob(struct symbol *sym)
{
	int size = sym->bit_size;
	int alignment = sym->ctype.alignment;
	const char *name = show_ident(sym->ident);

	if (size <= 0) {
		warning(sym->pos, "emitting insized symbol");
		size = 8;
	}
	if (size & 7)
		warning(sym->pos, "emitting symbol of size %d bits\n", size);
	size = (size+7) >> 3;
	if (alignment < 1)
		alignment = 1;
	if (!(size & (alignment-1))) {
		switch (alignment) {
		case 1:
			printf("unsigned char %s[%d];\n", name, size);
			return;
		case 2:
			printf("unsigned short %s[%d];\n", name, (size+1) >> 1);
			return;
		case 4:
			printf("unsigned int %s[%d];\n", name, (size+3) >> 2);
			return;
		}
	}
	printf("unsigned char %s[%d] __attribute__((aligned(%d)));\n",
		name, size, alignment);
	return;
}

static void emit_fn(struct symbol *sym)
{
	const char *name = show_ident(sym->ident);
	printf("%s();\n", name);	
}

void emit_symbol(struct symbol *sym)
{
	struct symbol *ctype;

	if (sym->type != SYM_NODE) {
		warning(sym->pos, "I really want to emit nodes, not pure types!");
		return;
	}

	ctype = sym->ctype.base_type;
	if (!ctype)
		return;
	switch (ctype->type) {
	case SYM_NODE:
	case SYM_PTR:
	case SYM_ARRAY:
	case SYM_STRUCT:
	case SYM_UNION:
	case SYM_BASETYPE:
		emit_blob(sym);
		return;
	case SYM_FN:
		emit_fn(sym);
		return;
	default:
		warning(sym->pos, "what kind of strange node do you want me to emit again?");
		return;
	}
}

static void emit_symbol_list(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		emit_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = argv[1];
	struct token *token;
	struct symbol_list *list;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", filename);

	// Initialize type system
	init_ctype();

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL, includepath);
	close(fd);

	// Pre-process the stream
	token = preprocess(token);

	// Parse the resulting C code
	list = translation_unit(token);

	// Show it
	emit_symbol_list(list);

	return 0;
}
