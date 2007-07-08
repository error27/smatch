#define a a|
#define b(x) x

b(a)
/*
 * check-name: Preprocessor #5
 * check-description: Yet more examples from comp.std.c.
 * check-command: sparse -E $file
 * check-exit-value: 0
 *
 * check-output-start

a|
 * check-output-end
 */
