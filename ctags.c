/*
 * Sparse Ctags
 *
 * Ctags generates tags from preprocessing results.
 *
 * Copyright (C) 2006 Christopher Li
 *
 * Licensed under the Open Software License version 1.1
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "parse.h"
#include "scope.h"

static struct symbol_list *taglist = NULL;

static void examine_symbol(struct symbol *sym);

#define MAX(_x,_y) ((_x) > (_y) ? (_x) : (_y))

static int cmp_sym(const void *m, const void *n)
{
	struct ident *a = ((struct symbol *)m)->ident;
	struct ident *b = ((struct symbol *)n)->ident;
	int ret = strncmp(a->name, b->name, MAX(a->len, b->len));
	if (!ret) {
		struct position a_pos = ((struct symbol *)m)->pos;
		struct position b_pos = ((struct symbol *)n)->pos;

		ret = strcmp(stream_name(a_pos.stream),
		             stream_name(b_pos.stream));
		if (!ret)
			return a_pos.line < b_pos.line;
	}
	return ret;
}

static void show_tag_header(FILE *fp)
{
	fprintf(fp, "!_TAG_FILE_FORMAT\t2\t/extended format; --format=1 will not append ;\" to lines/\n");
	fprintf(fp, "!_TAG_FILE_SORTED\t0\t/0=unsorted, 1=sorted, 2=foldcase/\n");
	fprintf(fp, "!_TAG_PROGRAM_AUTHOR\tChristopher Li\t/sparse@chrisli.org/\n");
	fprintf(fp, "!_TAG_PROGRAM_NAME\tSparse Ctags\t//\n");
	fprintf(fp, "!_TAG_PROGRAM_URL\thttp://kernel.org/pub/linux/kernel/people/josh/sparse/\t/official site/\n");
	fprintf(fp, "!_TAG_PROGRAM_VERSION\t0.01\t//\n");
}

static inline void show_symbol_tag(FILE *fp, struct symbol *sym)
{
	fprintf(fp, "%s\t%s\t%d;\"\t%c\tfile:\n", show_ident(sym->ident),
	       stream_name(sym->pos.stream), sym->pos.line, (int)sym->kind);
}

static void show_tags(struct symbol_list *list)
{
	struct symbol *sym;
	struct ident *ident = NULL;
	struct position pos = {};
	static const char *filename;
	FILE *fp;

	if (!list)
		return;

	fp = fopen("tags", "w");
	if (!fp) {
		perror("open tags file");
		return;
	}
	show_tag_header(fp);
	FOR_EACH_PTR(list, sym) {
		if (ident == sym->ident && pos.line == sym->pos.line &&
		    !strcmp(filename, stream_name(sym->pos.stream)))
			continue;

		show_symbol_tag(fp, sym);
		ident = sym->ident;
		pos = sym->pos;
		filename = stream_name(sym->pos.stream);
	} END_FOR_EACH_PTR(sym);
	fclose(fp);
}

static inline void add_tag(struct symbol *sym)
{
	if (sym->ident && !sym->visited) {
		sym->visited = 1;
		add_symbol(&taglist, sym);
	}
}

static inline void examine_members(struct symbol_list *list)
{
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		sym->kind = 'm';
		examine_symbol(sym);
	} END_FOR_EACH_PTR(sym);
}

static void examine_symbol(struct symbol *sym)
{
	struct symbol *base = sym;

	if (!sym || sym->visited)
		return;
	if (sym->ident && sym->ident->reserved)
		return;

	add_tag(sym);
	base = sym->ctype.base_type;

	switch (sym->type) {
	case SYM_NODE:
		if (base->type == SYM_FN)
			sym->kind = 'f';
		examine_symbol(base);
		break;
	case SYM_STRUCT:
		sym->kind = 's';
		examine_members(sym->symbol_list);
		break;
	case SYM_UNION:
		sym->kind = 'u';
		examine_members(sym->symbol_list);
		break;
	case SYM_ENUM:
		sym->kind = 'e';
	case SYM_PTR:
	case SYM_TYPEOF:
	case SYM_BITFIELD:
	case SYM_FN:
	case SYM_ARRAY:
		examine_symbol(sym->ctype.base_type);
		break;
	case SYM_BASETYPE:
		break;

	default:
		die("unknown symbol %s namespace:%d type:%d\n", show_ident(sym->ident),
		    sym->namespace, sym->type);
	}
	if (!sym->kind)
		sym->kind = 'v';
	return;
}

static void examine_namespace(struct symbol *sym)
{
	if (sym->visited)
		return;
	if (sym->ident && sym->ident->reserved)
		return;

	switch(sym->namespace) {
	case NS_LABEL:
		sym->kind = 'l';
		break;
	case NS_PREPROCESSOR:
		break;
	case NS_MACRO:
	case NS_UNDEF:
		sym->kind = 'd';
		break;
	case NS_TYPEDEF:
		sym->kind = 't';
	case NS_SYMBOL:
	case NS_STRUCT:
		examine_symbol(sym);
		break;
	default:
		die("unknown namespace %d symbol:%s type:%d\n", sym->namespace,
		    show_ident(sym->ident), sym->type);
	}
	add_tag(sym);
}

static inline void examine_symbol_list(struct symbol_list *list)
{
	struct symbol *sym;

	if (!list)
		return;
	FOR_EACH_PTR(list, sym) {
		examine_namespace(sym);
	} END_FOR_EACH_PTR(sym);
}

int main(int argc, char **argv)
{
	struct string_list *filelist = NULL;
	char *file;

	examine_symbol_list(sparse_initialize(argc, argv, &filelist));
	FOR_EACH_PTR_NOTAG(filelist, file) {
		sparse(file);
		examine_symbol_list(file_scope->symbols);
	} END_FOR_EACH_PTR_NOTAG(file);
	examine_symbol_list(global_scope->symbols);
	sort_list((struct ptr_list **)&taglist, cmp_sym);
	show_tags(taglist);
	return 0;
}
