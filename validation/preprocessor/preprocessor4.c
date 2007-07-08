#define foo bar
#define mac(x) x(foo)

mac(foo)

/*
 * check-name: Preprocessor #4
 * check-description: More examples from the comp.std.c discussion.
 * check-command: sparse -E $file
 * check-exit-value: 0
 *
 * check-output-start

bar(bar)
 * check-output-end
 */
