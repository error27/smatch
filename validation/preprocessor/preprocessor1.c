#define func(x) x
#define bar func(
#define foo bar foo
foo )
/*
 * check-name: Preprocessor #1
 * check-description: Used to cause infinite recursion.
 * check-command: sparse -E $file
 * check-exit-value: 0
 *
 * check-output-start

foo
 * check-output-end
 */
