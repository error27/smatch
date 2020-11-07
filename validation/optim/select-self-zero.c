int sel_self0x(int x) { return (x ? 0 : x) == 0; }
int sel_selfx0(int x) { return (x ? x : 0) == x; }

/*
 * check-name: select-self-zero
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
