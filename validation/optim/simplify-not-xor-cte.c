#define C 3

int foo(int x) { return ~(x ^ C) == (x ^ ~C); }

/*
 * check-name: simplify-not-xor-cte
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\..*\\$1
 */
