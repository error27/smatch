#define ZEXT(X)	((long long)(X))
#define BITS	((long long)(~0U))

int zext_ult(unsigned int x) { return (ZEXT(x) <  (BITS + 1)) == 1; }
int zext_ule(unsigned int x) { return (ZEXT(x) <= (BITS + 0)) == 1; }
int zext_uge(unsigned int x) { return (ZEXT(x) >= (BITS + 1)) == 0; }
int zext_ugt(unsigned int x) { return (ZEXT(x) >  (BITS + 0)) == 0; }

int zext_0le(unsigned int x) { return (ZEXT(x) <=  0) == (x == 0); }
int zext_0ge(unsigned int x) { return (ZEXT(x) >   0) == (x != 0); }

int zext_llt(unsigned int x) { return (ZEXT(x) <  -1) == 0; }
int zext_lle(unsigned int x) { return (ZEXT(x) <= -1) == 0; }
int zext_lge(unsigned int x) { return (ZEXT(x) >= -1) == 1; }
int zext_lgt(unsigned int x) { return (ZEXT(x) >  -1) == 1; }

/*
 * check-name: cmp-zext-simm
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
