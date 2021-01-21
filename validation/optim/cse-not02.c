int and(int a, int b) { return ((a == b) & (a != b)) == 0; }
int ior(int a, int b) { return ((a == b) | (a != b)) == 1; }
int xor(int a, int b) { return ((a == b) ^ (a != b)) == 1; }

/*
 * check-name: cse-not02
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
