int foo(int x) { return -(~x) == x + 1; }

/*
 * check-name: simplify-neg-not
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
