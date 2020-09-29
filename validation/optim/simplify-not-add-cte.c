#define C 3

int foo(int x) { return ~(x + C) == (~C - x); }

/*
 * check-name: simplify-not-add-cte
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
