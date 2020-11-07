int foo(int p, int a) { return (p ? a : a) == a; }

/*
 * check-name: select-same-args
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
