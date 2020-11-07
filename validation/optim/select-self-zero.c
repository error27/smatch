int sel_self0x(int x) { return (x ? 0 : x) == 0; }

/*
 * check-name: select-self-zero
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
