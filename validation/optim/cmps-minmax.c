#define SMAX __INT_MAX__
#define SMIN (-__INT_MAX__-1)

int lt_smin(int a) { return (a <  SMIN) + 1; }
int le_smax(int a) { return (a <= SMAX) + 0; }

int ge_smin(int a) { return (a >= SMIN) + 0; }
int gt_smax(int a) { return (a >  SMAX) + 1; }

/*
 * check-name: cmps-minmax
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
