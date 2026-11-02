/* Emit a first-draft C rendering of top-level Sparse symbols. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib.h"
#include "scope.h"
#include "symbol.h"

#define EXTRACT_BUFFER_SIZE (16 * 1024 * 1024)

static bool is_function_definition(struct symbol *symbol)
{
	struct symbol *type = symbol->ctype.base_type;

	return type && type->type == SYM_FN &&
		(type->stmt || type->inline_stmt);
}

static void show_all(FILE *out, struct symbol_list *list, char *buf)
{
	struct symbol *sym;

	show_all_type_definitions(file_scope->symbols, buf, EXTRACT_BUFFER_SIZE);
	fprintf(out, "%s\n", buf);
	FOR_EACH_PTR(list, sym) {
		show_sym(sym, buf, EXTRACT_BUFFER_SIZE);
		fprintf(out, "%s\n", buf);
	} END_FOR_EACH_PTR(sym);
}

static void show_function(FILE *out, struct symbol *function, char *buf)
{
	show_sym(function, buf, EXTRACT_BUFFER_SIZE);
	fprintf(out, "%s\n", buf);
}

static struct symbol *find_function(struct symbol_list *list, const char *name,
				    const char *file)
{
	struct symbol *sym;

	if (file) {
		FOR_EACH_PTR_REVERSE(list, sym) {
			if (is_function_definition(sym) && sym->ident &&
			    strcmp(stream_name(sym->pos.stream), file) == 0)
				return sym;
		} END_FOR_EACH_PTR_REVERSE(sym);
		return NULL;
	}
	FOR_EACH_PTR(list, sym) {
		if (is_function_definition(sym) && sym->ident &&
		    strcmp(sym->ident->name, name) == 0)
			return sym;
	} END_FOR_EACH_PTR(sym);
	return NULL;
}

int main(int argc, char **argv)
{
	struct string_list *files = NULL;
	struct symbol_list *list;
	struct symbol *function;
	bool whole_file;
	bool last_func;
	bool minimal;
	char *file;
	char *root_name;
	char output_file[PATH_MAX];
	char *buf;
	FILE *out;

	if (argc < 2)
		die("usage: extract [--func=FUNCTION|--last-func] [--minimal] [sparse options] file.c");
	root_name = NULL;
	last_func = false;
	minimal = false;
	while (argc > 1) {
		if (strcmp(argv[1], "--last-func") == 0) {
			last_func = true;
		} else if (strcmp(argv[1], "--minimal") == 0) {
			minimal = true;
		} else if (strncmp(argv[1], "--func=", 7) == 0) {
			if (argv[1][7] == '\0')
				die("function name is empty");
			root_name = argv[1] + 7;
		} else {
			break;
		}
		memmove(argv + 1, argv + 2, (argc - 2) * sizeof(*argv));
		argc--;
		argv[argc] = NULL;
	}
	whole_file = root_name == NULL && !last_func;
	if (minimal && whole_file)
		die("--minimal needs --func=FUNCTION or --last-func");
	buf = calloc(1, EXTRACT_BUFFER_SIZE);
	if (!buf)
		die("out of memory");
	preserve_builtin_va_calls = true;
	preserve_unevaluated_symbols = true;
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
	else if (last_func)
		snprintf(output_file, sizeof(output_file), "%s.extract.last", file);
	else
		snprintf(output_file, sizeof(output_file), "%s.extract.%s", file,
			 root_name);
	out = fopen(output_file, "w");
	if (!out)
		die("cannot write %s", output_file);

	list = sparse_keep_tokens(file);
	if (whole_file) {
		fprintf(out, "typedef _Bool bool;\n\n");
		show_all(out, list, buf);
	} else {
		function = find_function(list, root_name, last_func ? file : NULL);
		if (!function) {
			if (last_func) {
				fprintf(stderr, "warning: no function was found in %s\n", file);
				fclose(out);
				free(buf);
				printf("extract: wrote %s\n", output_file);
				return 0;
			}
			die("function %s was not found", root_name);
		}
		if (minimal)
			show_code_minimal_root = function;
		fprintf(out, "typedef _Bool bool;\n\n");
		show_function(out, function, buf);
	}
	fclose(out);
	free(buf);
	printf("extract: wrote %s\n", output_file);
	return 0;
}
