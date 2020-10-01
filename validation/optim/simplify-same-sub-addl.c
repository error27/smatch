int foo(int x, int y) { return (x - y) + y; }

/*
 * check-name: simplify-same-sub-addl
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-contains: ret\\..*%arg1
 */
