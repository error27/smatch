#define EQ(X)		+ (X == 0)
#define MASK		32U


int cmpu_ior_lt_lt(int a) { return ((a | MASK) <  (MASK - 1)) EQ(0); }
int cmpu_ior_lt_eq(int a) { return ((a | MASK) <  (MASK    )) EQ(0); }
int cmpu_ior_le_lt(int a) { return ((a | MASK) <= (MASK - 1)) EQ(0); }
int cmpu_ior_ge_lt(int a) { return ((a | MASK) >= (MASK - 1)) EQ(1); }
int cmpu_ior_ge_eq(int a) { return ((a | MASK) >= (MASK    )) EQ(1); }
int cmpu_ior_gt_lt(int a) { return ((a | MASK) >  (MASK - 1)) EQ(1); }

/*
 * check-name: cmpu-or0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
