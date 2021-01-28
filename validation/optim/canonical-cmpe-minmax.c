#define SMAX __INT_MAX__
#define SMIN (-__INT_MAX__-1)

int le_smax(int a) { return (a <= (SMAX - 1)) == (a != SMAX); }
int gt_smax(int a) { return (a >  (SMAX - 1)) == (a == SMAX); }

int lt_smin(int a) { return (a <  (SMIN + 1)) == (a == SMIN); }
int ge_smin(int a) { return (a >= (SMIN + 1)) == (a != SMIN); }

/*
 * check-name: canonical-cmpe-minmax
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
