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
#include "linearize.h"

static void emit_entrypoint(struct entrypoint *ep)
{
	
}

static void emit_symbol(struct symbol *sym)
{
	struct entrypoint *ep;
	ep = linearize_symbol(sym);
	if (ep)
		emit_entrypoint(ep);
}

static void emit_symbol_list(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		expand_symbol(sym);
		emit_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	emit_symbol_list(sparse(argc, argv));
	return 0;
}
