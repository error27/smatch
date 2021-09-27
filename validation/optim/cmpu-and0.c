#define MASK		32U


int cmps_and_ltu_gt(int a) { return ((a & MASK) <  (MASK + 1)) + 0; }
int cmps_and_leu_gt(int a) { return ((a & MASK) <= (MASK + 1)) + 0; }
int cmps_and_leu_eq(int a) { return ((a & MASK) <= (MASK + 0)) + 0; }
int cmps_and_geu_gt(int a) { return ((a & MASK) >= (MASK + 1)) + 1; }
int cmps_and_gtu_gt(int a) { return ((a & MASK) >  (MASK + 1)) + 1; }
int cmps_and_gtu_eq(int a) { return ((a & MASK) >  (MASK + 0)) + 1; }

/*
 * check-name: cmpu-and0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
