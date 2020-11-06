int canon_not(int a, int b) { return (a & ~b) == (~b & a); }

/*
 * check-name: canonical-not
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
