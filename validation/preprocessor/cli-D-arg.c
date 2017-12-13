A
B
/*
 * check-name: cli: -D MACRO
 * check-command: sparse -E -D A -D B=abc $file
 * check-known-to-fail
 *
 * check-output-start

1
abc
 * check-output-end
 */
