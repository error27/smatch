#define C 3

int foo(int x) { return -(x + C) == (-3 - x); }

/*
 * check-name: simplify-neg-add-cte
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
