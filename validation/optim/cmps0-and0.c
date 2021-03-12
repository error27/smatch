#define M		32

int cmps_and_sle0(int a) { return ((a & M) <= 0) == ((a & M) == 0); }
int cmps_and_sgt0(int a) { return ((a & M) >  0) == ((a & M) != 0); }

/*
 * check-name: cmps0-and
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
