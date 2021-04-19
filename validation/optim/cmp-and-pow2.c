#define M 32

_Bool eq(int a) { return ((a & M) != M) == ((a & M) == 0); }
_Bool ne(int a) { return ((a & M) == M) == ((a & M) != 0); }

/*
 * check-name: cmp-and-pow2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
