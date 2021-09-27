#define EQ(X)		+ (X == 0)
#define SIGN		(1 << 31)
#define MASK		(SIGN | 32)


int cmps_ior_lt_x(int a) { return ((a | MASK) <  4) EQ(1); }
int cmps_ior_lt_0(int a) { return ((a | MASK) <  0) EQ(1); }
int cmps_ior_le_x(int a) { return ((a | MASK) <= 4) EQ(1); }
int cmps_ior_le_0(int a) { return ((a | MASK) <= 0) EQ(1); }
int cmps_ior_ge_x(int a) { return ((a | MASK) >= 4) EQ(0); }
int cmps_ior_ge_0(int a) { return ((a | MASK) >= 0) EQ(0); }
int cmps_ior_gt_x(int a) { return ((a | MASK) >  4) EQ(0); }
int cmps_ior_gt_0(int a) { return ((a | MASK) >  0) EQ(0); }

/*
 * check-name: cmps-or0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
