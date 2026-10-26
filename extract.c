/* Emit a first-draft C rendering of top-level Sparse symbols. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "scope.h"
#include "symbol.h"

#define EXTRACT_BUFFER_SIZE (16 * 1024 * 1024)

struct inline_functions {
	struct ident *idents[8192];
	unsigned int count;
};

static bool is_inline_function(struct symbol *symbol)
{
	struct symbol *type = symbol->ctype.base_type;

	return type && type->type == SYM_FN && type->inline_stmt;
}

static void collect_inline_functions(struct symbol_list *symbols, char *buf)
{
	struct symbol *symbol;

	FOR_EACH_PTR(symbols, symbol) {
		show_sym(symbol, buf, EXTRACT_BUFFER_SIZE);
	} END_FOR_EACH_PTR(symbol);
}

static bool inline_function_was_output(struct inline_functions *functions,
					       struct symbol *symbol)
{
	unsigned int i;

	if (!symbol->ident)
		return false;
	for (i = 0; i < functions->count; i++)
		if (functions->idents[i] == symbol->ident ||
		    strcmp(show_ident(functions->idents[i]),
			   show_ident(symbol->ident)) == 0)
			return true;
	return false;
}

static void show_inline_function_definitions(FILE *out,
					     struct symbol_list *symbols, char *buf,
					     struct inline_functions *functions)
{
	struct symbol *symbol;

	FOR_EACH_PTR_REVERSE(symbols, symbol) {
		if (!is_inline_function(symbol) ||
		    inline_function_was_output(functions, symbol))
			continue;
		if (functions->count < ARRAY_SIZE(functions->idents))
			functions->idents[functions->count++] = symbol->ident;
		show_sym(symbol, buf, EXTRACT_BUFFER_SIZE);
		fprintf(out, "%s\n", buf);
	} END_FOR_EACH_PTR_REVERSE(symbol);
}

int main(int argc, char **argv)
{
	struct string_list *files = NULL;
	struct symbol_list *list;
	struct symbol *sym;
	bool whole_file;
	bool found;
	char *file;
	char *root_name;
	char output_file[PATH_MAX];
	char *buf;
	FILE *out;
	struct inline_functions inline_functions = { };

	if (argc < 2)
		die("usage: extract [--func=FUNCTION] [sparse options] file.c");
	root_name = NULL;
	if (strncmp(argv[1], "--func=", 7) == 0) {
		if (argv[1][7] == '\0')
			die("function name is empty");
		root_name = argv[1] + 7;
		memmove(argv + 1, argv + 2, (argc - 2) * sizeof(*argv));
		argc--;
		argv[argc] = NULL;
	}
	whole_file = root_name == NULL;
	found = whole_file;

	buf = calloc(1, EXTRACT_BUFFER_SIZE);
	if (!buf)
		die("out of memory");
	sparse_initialize(argc, argv, &files);
	file = first_ptr_list((struct ptr_list *)files);
	if (!file)
		die("no source file");
	if (strcmp(file, "/dev/null") == 0) {
		free(buf);
		return 0;
	}
	if (whole_file)
		snprintf(output_file, sizeof(output_file), "%s.extract", file);
	else
		snprintf(output_file, sizeof(output_file), "%s.extract.%s", file,
			 root_name);
	out = fopen(output_file, "w");
	if (!out)
		die("cannot write %s", output_file);
	fprintf(out, "#define bool _Bool\n\n");

	list = sparse_keep_tokens(file);
	collect_inline_functions(list, buf);
	buf[0] = '\0';
	show_all_type_definitions(file_scope->symbols, buf, EXTRACT_BUFFER_SIZE);
	fprintf(out, "%s\n", buf);
	show_inline_function_definitions(out, list, buf, &inline_functions);
	FOR_EACH_PTR(list, sym) {
		struct ident *ident = sym->ident;

		if (is_inline_function(sym) ||
		    inline_function_was_output(&inline_functions, sym))
			continue;
		show_sym(sym, buf, EXTRACT_BUFFER_SIZE);
		fprintf(out, "%s\n", buf);
		if (!whole_file && ident && strcmp(ident->name, root_name) == 0) {
			found = true;
			goto output_done;
		}
	} END_FOR_EACH_PTR(sym);
output_done:
	fclose(out);
	free(buf);
	if (!found)
		die("function %s was not found", root_name);
	printf("extract: wrote %s\n", output_file);
	return 0;
}
