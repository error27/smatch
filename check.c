/*
 * Example trivial client program that uses the sparse library
 * to tokenize, pre-process and parse a C file, and prints out
 * the results.
 *
 * Copyright (C) 2003 Transmeta Corp.
 *               2003 Linus Torvalds
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

static int context_increase(struct basic_block *bb)
{
	int sum = 0;
	struct instruction *insn;

	FOR_EACH_PTR(bb->insns, insn) {
		if (insn->opcode == OP_CONTEXT)
			sum += insn->increment;
	} END_FOR_EACH_PTR(insn);
	return sum;
}

static int imbalance(struct entrypoint *ep, struct basic_block *bb, int entry, int exit, const char *why)
{
	if (Wcontext) {
		struct symbol *sym = ep->name;
		warning(bb->pos, "context imbalance in '%s' - %s", show_ident(sym->ident), why);
	}
	return -1;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb, int entry, int exit);

static int check_children(struct entrypoint *ep, struct basic_block *bb, int entry, int exit)
{
	struct terminator_iterator term;
	struct instruction *insn;
	struct basic_block *child;

	insn = last_instruction(bb->insns);
	if (!insn)
		return 0;
	if (insn->opcode == OP_RET)
		return entry != exit ? imbalance(ep, bb, entry, exit, "wrong count at exit") : 0;

	init_terminator_iterator(insn, &term);
	while ((child=next_terminator_bb(&term)) != NULL) {
		if (check_bb_context(ep, child, entry, exit))
			return -1;
	}
	return 0;
}

static int check_bb_context(struct entrypoint *ep, struct basic_block *bb, int entry, int exit)
{
	if (!bb)
		return 0;
	if (bb->context == entry)
		return 0;

	/* Now that's not good.. */
	if (bb->context >= 0)
		return imbalance(ep, bb, entry, bb->context, "different lock contexts for basic block");

	bb->context = entry;
	entry += context_increase(bb);
	if (entry < 0)
		return imbalance(ep, bb, entry, exit, "unexpected unlock");

	return check_children(ep, bb, entry, exit);
}

static void check_context(struct entrypoint *ep)
{
	struct symbol *sym = ep->name;

	check_bb_context(ep, ep->entry, sym->ctype.in_context, sym->ctype.out_context);
}

static void clean_up_symbols(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		struct entrypoint *ep;

		evaluate_symbol(sym);
		check_duplicates(sym);
		expand_symbol(sym);
		ep = linearize_symbol(sym);
		if (ep)
			check_context(ep);
	} END_FOR_EACH_PTR(sym);
}

static void do_predefined(char *filename)
{
	add_pre_buffer("#define __BASE_FILE__ \"%s\"\n", filename);
	add_pre_buffer("#define __DATE__ \"??? ?? ????\"\n");
	add_pre_buffer("#define __TIME__ \"??:??:??\"\n");
}

int main(int argc, char **argv)
{
	int fd;
	char *filename = NULL, **args;
	struct token *token;

	// Initialize symbol stream first, so that we can add defines etc
	init_symbols();

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

	if (!filename)
		die("no input files given");

	// Initialize type system
	init_ctype();

	create_builtin_stream();
	add_pre_buffer("#define __CHECKER__ 1\n");
	if (!preprocess_only)
		declare_builtin_functions();

	do_predefined(filename);

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		die("No such file: %s", filename);

	// Tokenize the input stream
	token = tokenize(filename, fd, NULL, includepath);
	close(fd);

	// Prepend any "include" file to the stream.
	if (include_fd >= 0)
		token = tokenize(include, include_fd, token, includepath);

	// Prepend the initial built-in stream
	token = tokenize_buffer(pre_buffer, pre_buffer_size, token);

	// Pre-process the stream
	token = preprocess(token);

	if (preprocess_only) {
		while (!eof_token(token)) {
			int prec = 1;
			struct token *next = token->next;
			const char *separator = "";
			if (next->pos.whitespace)
				separator = " ";
			if (next->pos.newline) {
				separator = "\n\t\t\t\t\t";
				prec = next->pos.pos;
				if (prec > 4)
					prec = 4;
			}
			printf("%s%.*s", show_token(token), prec, separator);
			token = next;
		}
		putchar('\n');

		return 0;
	} 

	// Parse the resulting C code
	translation_unit(token, &used_list);

	// Do type evaluation and simplify
	clean_up_symbols(used_list);
	return 0;
}
