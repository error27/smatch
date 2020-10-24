int foo(int x, int y) { return -(x - y) == (y - x); }

/*
 * check-name: simplify-neg-sub
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
