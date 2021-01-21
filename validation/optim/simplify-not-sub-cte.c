#define C 3

int foo(int x) { return ~(C - x) == (x + ~C); }

/*
 * check-name: simplify-not-sub-cte
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
