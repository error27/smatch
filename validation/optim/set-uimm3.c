int le(int x) { return (x <= 0x7fffffffU) == (x >= 0); }
int gt(int x) { return (x >  0x7fffffffU) == (x <  0); }

/*
 * check-name: set-uimm3
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
