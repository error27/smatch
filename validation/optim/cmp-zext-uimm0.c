#define zext(X)	((unsigned long long) (X))
#define MAX	(1ULL << 32)

#define TEST(X,OP,VAL)	(zext(X) OP (VAL)) == (X OP (VAL))

int zext_ltu_0(unsigned int x) { return TEST(x, < , MAX); }
int zext_ltu_m(unsigned int x) { return TEST(x, < , MAX - 1); }
int zext_lte_0(unsigned int x) { return TEST(x, <=, MAX); }
int zext_lte_m(unsigned int x) { return TEST(x, <=, MAX - 1); }
int zext_gte_0(unsigned int x) { return TEST(x, >=, MAX); }
int zext_gte_m(unsigned int x) { return TEST(x, >=, MAX - 1); }
int zext_gtu_0(unsigned int x) { return TEST(x, > , MAX); }
int zext_gtu_m(unsigned int x) { return TEST(x, > , MAX - 1); }

/*
 * check-name: cmp-zext-uimm0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
