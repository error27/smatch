/* Emit a first-draft C rendering of top-level Sparse symbols. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static void write_function(const char *output_file, struct symbol *function,
			   bool minimal, char *buf)
{
	FILE *out;

	out = fopen(output_file, "w");
	if (!out)
		die("cannot write %s", output_file);
	show_code_reset();
	if (minimal)
		show_code_minimal_root = function;
	fprintf(out, "typedef _Bool bool;\n\n");
	show_function(out, function, buf);
	fclose(out);
	printf("extract: wrote %s\n", output_file);
}

/* Printing can add inline functions to the list, so return a copy. */
static struct symbol_list *file_functions(struct symbol_list *list,
					  const char *file)
{
	struct symbol_list *functions = NULL;
	struct symbol *sym;

	FOR_EACH_PTR(list, sym) {
		if (is_function_definition(sym) && sym->ident &&
		    strcmp(stream_name(sym->pos.stream), file) == 0)
			add_symbol(&functions, sym);
	} END_FOR_EACH_PTR(sym);
	return functions;
}

static void list_functions(struct symbol_list *list, const char *file)
{
	struct symbol_list *functions = file_functions(list, file);
	struct symbol *sym;

	FOR_EACH_PTR(functions, sym) {
		printf("%s\n", show_ident(sym->ident));
	} END_FOR_EACH_PTR(sym);
	free_ptr_list(&functions);
}

static void write_all_functions(struct symbol_list *list, const char *file,
				bool minimal, char *buf)
{
	char output_file[PATH_MAX];
	struct symbol_list *functions = file_functions(list, file);
	struct symbol *sym;

	FOR_EACH_PTR(functions, sym) {
		snprintf(output_file, sizeof(output_file), "%s.extract.%s",
			 file, show_ident(sym->ident));
		write_function(output_file, sym, minimal, buf);
	} END_FOR_EACH_PTR(sym);
	free_ptr_list(&functions);
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

/* Terminate the first name of a comma separated list and return the rest. */
static char *split_name(char *names)
{
	char *comma = strchr(names, ',');

	if (comma)
		*comma++ = '\0';
	if (names[0] == '\0' || (comma && comma[0] == '\0'))
		die("function name is empty");
	return comma;
}

/*
 * Put the output in out_dir under the source path, so files with the same
 * name in different directories do not collide.
 */
static void out_dir_path(char *path, size_t size, const char *out_dir,
			 const char *file)
{
	const char *p;
	char *slash;

	while (file[0] == '/' || strncmp(file, "./", 2) == 0)
		file += file[0] == '/' ? 1 : 2;
	for (p = file; p; p = strchr(p, '/')) {
		if (p[0] == '/')
			p++;
		if (strncmp(p, "../", 3) == 0)
			die("--out-dir cannot be used with a path containing \"..\"");
	}
	if (snprintf(path, size, "%s/%s", out_dir, file) >= size)
		die("output path is too long");

	slash = path;
	while ((slash = strchr(slash + 1, '/'))) {
		*slash = '\0';
		if (mkdir(path, 0777) != 0 && errno != EEXIST)
			die("cannot create directory %s", path);
		*slash = '/';
	}
}

int main(int argc, char **argv)
{
	struct string_list *files = NULL;
	struct symbol_list *list;
	struct symbol *function;
	bool whole_file;
	bool all_funcs;
	bool list_funcs;
	bool last_func;
	bool minimal;
	char *file;
	char *root_name;
	char *out_dir;
	char *next_name;
	char *name;
	char output_file[PATH_MAX];
	char *buf;
	FILE *out;

	if (argc < 2)
		die("usage: extract [--func=FUNCTION[,FUNCTION...]|--last-func|--all-funcs|--list-funcs] [--minimal] [--out-dir=DIR] [sparse options] file.c");
	root_name = NULL;
	out_dir = NULL;
	all_funcs = false;
	list_funcs = false;
	last_func = false;
	minimal = false;
	while (argc > 1) {
		if (strcmp(argv[1], "--all-funcs") == 0) {
			all_funcs = true;
		} else if (strcmp(argv[1], "--list-funcs") == 0) {
			list_funcs = true;
		} else if (strcmp(argv[1], "--last-func") == 0) {
			last_func = true;
		} else if (strcmp(argv[1], "--minimal") == 0) {
			minimal = true;
		} else if (strncmp(argv[1], "--func=", 7) == 0) {
			if (argv[1][7] == '\0')
				die("function name is empty");
			root_name = argv[1] + 7;
		} else if (strncmp(argv[1], "--out-dir=", 10) == 0) {
			if (argv[1][10] == '\0')
				die("output directory is empty");
			out_dir = argv[1] + 10;
		} else {
			break;
		}
		memmove(argv + 1, argv + 2, (argc - 2) * sizeof(*argv));
		argc--;
		argv[argc] = NULL;
	}
	if (all_funcs + list_funcs + last_func + (root_name != NULL) > 1)
		die("use only one of --func=FUNCTION, --last-func, --all-funcs and --list-funcs");
	whole_file = root_name == NULL && !last_func && !all_funcs && !list_funcs;
	if (out_dir && (all_funcs || list_funcs))
		die("--out-dir cannot be used with --all-funcs or --list-funcs");
	if (minimal && root_name && strchr(root_name, ','))
		die("--minimal takes only one function");
	if (minimal && whole_file)
		die("--minimal needs --func=FUNCTION, --last-func or --all-funcs");
	buf = calloc(1, EXTRACT_BUFFER_SIZE);
	if (!buf)
		die("out of memory");
	preserve_builtin_va_calls = true;
	preserve_unevaluated_symbols = true;
	predefine_no_checker = true;
	sparse_initialize(argc, argv, &files);
	file = first_ptr_list((struct ptr_list *)files);
	if (!file)
		die("no source file");
	if (strcmp(file, "/dev/null") == 0) {
		free(buf);
		return 0;
	}
	if (list_funcs) {
		list = sparse_keep_tokens(file);
		list_functions(list, file);
		free(buf);
		return 0;
	}
	if (all_funcs) {
		list = sparse_keep_tokens(file);
		write_all_functions(list, file, minimal, buf);
		free(buf);
		return 0;
	}
	if (out_dir) {
		struct stat src, dst;

		out_dir_path(output_file, sizeof(output_file), out_dir, file);
		if (stat(file, &src) == 0 && stat(output_file, &dst) == 0 &&
		    src.st_dev == dst.st_dev && src.st_ino == dst.st_ino)
			die("%s would overwrite the source file", output_file);
	} else if (whole_file) {
		snprintf(output_file, sizeof(output_file), "%s.extract", file);
	} else if (last_func) {
		snprintf(output_file, sizeof(output_file), "%s.extract.last", file);
	} else {
		snprintf(output_file, sizeof(output_file), "%s.extract.%s", file,
			 root_name);
	}
	out = fopen(output_file, "w");
	if (!out)
		die("cannot write %s", output_file);

	list = sparse_keep_tokens(file);
	if (whole_file) {
		fprintf(out, "typedef _Bool bool;\n\n");
		show_all(out, list, buf);
	} else {
		next_name = NULL;
		if (root_name)
			next_name = split_name(root_name);
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
		while (next_name) {
			name = next_name;
			next_name = split_name(name);
			function = find_function(list, name, NULL);
			if (!function)
				die("function %s was not found", name);
			show_function(out, function, buf);
		}
	}
	fclose(out);
	free(buf);
	printf("extract: wrote %s\n", output_file);
	return 0;
}
