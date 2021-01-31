int cmp_eq(int a) { return ((a | 1) != 0) + 0; }
int cmp_ne(int a) { return ((a | 1) == 0) + 1; }

/*
 * check-name: cmpe-or0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
