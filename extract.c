/* Emit a first-draft C rendering of top-level Sparse symbols. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "scope.h"
#include "symbol.h"

#define EXTRACT_BUFFER_SIZE (16 * 1024 * 1024)

static bool is_inline_function(struct symbol *symbol)
{
	struct symbol *type = symbol->ctype.base_type;

	return type && type->type == SYM_FN && type->inline_stmt;
}

int main(int argc, char **argv)
{
	struct string_list *files = NULL;
	struct symbol_list *list;
	struct symbol_list *inline_dependencies = NULL;
	struct symbol *sym;
	bool whole_file;
	bool found;
	char *file;
	char *root_name;
	char output_file[PATH_MAX];
	char *buf;
	FILE *out;

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
	preserve_builtin_va_calls = true;
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
	show_all_type_definitions(file_scope->symbols, buf, EXTRACT_BUFFER_SIZE);
	fprintf(out, "%s\n", buf);
	show_inline_function_definitions(list, &inline_dependencies, buf,
					 EXTRACT_BUFFER_SIZE);
	fprintf(out, "%s\n", buf);
	FOR_EACH_PTR(list, sym) {
		struct ident *ident = sym->ident;

		if (is_inline_function(sym) ||
		    lookup_ptr_list_entry((struct ptr_list *)inline_dependencies, sym))
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
