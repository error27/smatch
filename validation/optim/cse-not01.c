int and(int a) { return (~a & a) ==  0; }
int ior(int a) { return (~a | a) == ~0; }
int xor(int a) { return (~a ^ a) == ~0; }

/*
 * check-name: cse-not01
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
