int foo(int p, int a, int b) { return ((p ? 42 : 43) ? a : b) == a ; }

/*
 * check-name: select-select-true-true
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
