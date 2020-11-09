int t(int p, int a, int b) { return ((p == p) ? a : b) == a; }
int f(int p, int a, int b) { return ((p != p) ? a : b) == b; }

/*
 * check-name: select-constant-cond
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
