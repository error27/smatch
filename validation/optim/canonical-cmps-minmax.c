#define SMAX __INT_MAX__
#define SMIN (-__INT_MAX__-1)

int lt_smax(int a) { return (a <  SMAX) == (a != SMAX); }
int ge_smax(int a) { return (a >= SMAX) == (a == SMAX); }

int le_smin(int a) { return (a <= SMIN) == (a == SMIN); }
int gt_smin(int a) { return (a >  SMIN) == (a != SMIN); }

/*
 * check-name: canonical-cmps-minmax
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
