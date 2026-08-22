/*
 * Sparse identifier tagger
 *
 * Copyright (C) 2026 Dan Carpenter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dissect.h"
#include "options.h"

struct macro_use {
	struct position pos;
	const char *name;
	struct macro_use *next;
};

static struct string_list *source_files;
static struct macro_use *macro_uses;

static int source_position(struct position *pos)
{
	const char *file;
	char *source;

	file = stream_name(pos->stream);
	FOR_EACH_PTR(source_files, source) {
		if (!strcmp(file, source))
			return 1;
	} END_FOR_EACH_PTR(source);

	return 0;
}

static int seen_macro(struct position *pos, const char *name)
{
	struct macro_use *use;

	for (use = macro_uses; use; use = use->next) {
		if (use->pos.stream == pos->stream &&
		    use->pos.line == pos->line && use->pos.pos == pos->pos &&
		    !strcmp(use->name, name))
			return 1;
	}

	use = malloc(sizeof(*use));
	if (!use)
		die("out of memory\n");
	use->pos = *pos;
	use->name = name;
	use->next = macro_uses;
	macro_uses = use;

	return 0;
}

static int show_macro(struct position *pos)
{
	struct symbol *sym;
	const char *name;

	name = get_macro_name(*pos);
	if (!name)
		return 0;
	sym = lookup_macro_symbol(name);
	if (!sym)
		return 0;
	if (seen_macro(pos, name))
		return 1;

	printf("%s %s %d %d %s %d %d\n",
	       stream_name(pos->stream), name, pos->line, pos->pos,
	       stream_name(sym->pos.stream), sym->pos.line, sym->pos.pos);
	return 1;
}

static void show_identifier(struct position *pos, struct symbol *sym)
{
	struct ident *ident;

	if (!source_position(pos))
		return;
	if (show_macro(pos))
		return;
	if (!sym)
		return;
	ident = sym->ident;
	if (!ident || ident->reserved)
		return;

	printf("%s %.*s %d %d %s %d %d\n",
	       stream_name(pos->stream), ident->len, ident->name,
	       pos->line, pos->pos, stream_name(sym->pos.stream),
	       sym->pos.line, sym->pos.pos);
}

static void report_symbol_definition(struct symbol *sym)
{
	show_identifier(&sym->pos, sym);
}

static void report_member_definition(struct symbol *sym, struct symbol *member)
{
	show_identifier(&member->pos, member);
}

static void report_symbol(unsigned mode, struct position *pos,
			  struct symbol *sym)
{
	show_identifier(pos, sym);
}

static void report_member(unsigned mode, struct position *pos,
			  struct symbol *sym, struct symbol *member)
{
	show_identifier(pos, member);
}

int main(int argc, char **argv)
{
	static struct reporter reporter = {
		.r_memdef = report_member_definition,
		.r_member = report_member,
		.r_symdef = report_symbol_definition,
		.r_symbol = report_symbol,
	};
	struct string_list *filelist = NULL;

	sparse_initialize(argc, argv, &filelist);
	source_files = filelist;
	dissect_show_all_symbols = 1;
	dissect(&reporter, filelist);

	return 0;
}
