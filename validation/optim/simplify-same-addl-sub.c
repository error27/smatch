int foo(int x, int y) { return x + (y - x); }

/*
 * check-name: simplify-same-addl-sub
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*%arg2
 */
