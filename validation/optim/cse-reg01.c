int foo(int a, int b) { int x = a + b, y = ~b; return (x < y) == (y > x); }

/*
 * check-name: cse-reg01
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-returns: 1
 */
