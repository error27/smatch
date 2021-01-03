int cmpe_and_eq(int a) { return ((a & 0xff00) == 0xff01) + 1; }
int cmpe_and_ne(int a) { return ((a & 0xff00) != 0xff01) + 0; }

/*
 * check-name: cmpe-and0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
