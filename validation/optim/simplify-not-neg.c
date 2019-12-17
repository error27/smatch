int foo(int x) { return ~(-x) == (x - 1); }

/*
 * check-name: simplify-not-neg
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
