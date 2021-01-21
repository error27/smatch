int sel_eq01(int a, int b) { return ((a == b) ? a : b) == b; }
int sel_eq10(int a, int b) { return ((a == b) ? b : a) == a; }
int sel_ne01(int a, int b) { return ((a != b) ? a : b) == a; }
int sel_ne10(int a, int b) { return ((a != b) ? b : a) == b; }

/*
 * check-name: eqne-select
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
