#define MINUS_ONE	-1
#define MASK		32


int cmps_and_lt_lt0(int a) { return ((a & MASK) <  MINUS_ONE)  + 1; }
int cmps_and_lt_gtm(int a) { return ((a & MASK) <  (MASK + 1)) + 0; }
int cmps_and_le_lt0(int a) { return ((a & MASK) <= MINUS_ONE)  + 1; }
int cmps_and_le_gtm(int a) { return ((a & MASK) <= (MASK + 1)) + 0; }

int cmps_and_gt_lt0(int a) { return ((a & MASK) >  MINUS_ONE)  + 0; }
int cmps_and_gt_gtm(int a) { return ((a & MASK) >  (MASK + 1)) + 1; }
int cmps_and_ge_lt0(int a) { return ((a & MASK) >= MINUS_ONE)  + 0; }
int cmps_and_ge_gtm(int a) { return ((a & MASK) >= (MASK + 1)) + 1; }

/*
 * check-name: cmps-and0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
