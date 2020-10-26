#define zext(X)	((unsigned long long) (X))
#define BITS	((1ULL << 32) - 1)

int zext_lt_p(unsigned int x) { return (zext(x) <  (BITS + 1)) == 1; }
int zext_le_p(unsigned int x) { return (zext(x) <= (BITS    )) == 1; }
int zext_ge_p(unsigned int x) { return (zext(x) >= (BITS + 1)) == 0; }
int zext_gt_p(unsigned int x) { return (zext(x) >  (BITS    )) == 0; }

/*
 * check-name: cmp-zext-uimm1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
