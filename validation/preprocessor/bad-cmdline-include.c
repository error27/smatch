#error some random error

/*
 * check-name: bad-cmdline-include
 * check-command: sparse -include $file
 *
 * check-error-start
command-line: note: in included file (through builtin):
preprocessor/bad-cmdline-include.c:1:2: error: some random error
 * check-error-end
 */
