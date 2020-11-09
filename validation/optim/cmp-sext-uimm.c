#define sext(X)	((unsigned long long) (X))
#define POS	(1ULL << 31)
#define NEG	((unsigned long long) -POS)

int sext_ltu_p2(int x) { return (sext(x) <  (POS + 2)) == (x >= 0); }
int sext_ltu_p1(int x) { return (sext(x) <  (POS + 1)) == (x >= 0); }
int sext_ltu_p0(int x) { return (sext(x) <  (POS + 0)) == (x >= 0); }

int sext_leu_p1(int x) { return (sext(x) <= (POS + 1)) == (x >= 0); }
int sext_leu_p0(int x) { return (sext(x) <= (POS + 0)) == (x >= 0); }

int sext_geu_m1(int x) { return (sext(x) >= (NEG - 1)) == (x < 0); }
int sext_geu_m2(int x) { return (sext(x) >= (NEG - 2)) == (x < 0); }

int sext_gtu_m1(int x) { return (sext(x) > (NEG - 1)) == (x < 0); }
int sext_gtu_m2(int x) { return (sext(x) > (NEG - 2)) == (x < 0); }
int sext_gtu_m3(int x) { return (sext(x) > (NEG - 3)) == (x < 0); }

/*
 * check-name: cmp-sext-uimm
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
