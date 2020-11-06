int foo(int a, int b) { return (a < b) == (b > a); }

/*
 * check-name: cse-arg01
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-returns: 1
 */
