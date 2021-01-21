int fw(int p, int a, int b) { return ((p ? 42 : 0) ? a : b) == ( p ? a : b); }
int bw(int p, int a, int b) { return ((p ? 0 : 42) ? a : b) == ( p ? b : a); }

/*
 * check-name: select-select-true-false0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
