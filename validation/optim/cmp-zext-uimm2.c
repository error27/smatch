#define zext(X)	((unsigned long long) (X))

int zext_ltu_q(unsigned x) { return (zext(x) <  0x100000001UL) == 1; }
int zext_ltu_p(unsigned x) { return (zext(x) <  0x100000000UL) == 1; }
int zext_ltu_0(unsigned x) { return (zext(x) <  0x0ffffffffUL) == (x <  0xffffffff); }
int zext_ltu_m(unsigned x) { return (zext(x) <  0x0fffffffeUL) == (x <  0xfffffffe); }

int zext_leu_q(unsigned x) { return (zext(x) <= 0x100000001UL) == 1; }
int zext_leu_p(unsigned x) { return (zext(x) <= 0x100000000UL) == 1; }
int zext_leu_0(unsigned x) { return (zext(x) <= 0x0ffffffffUL) == 1; }
int zext_leu_m(unsigned x) { return (zext(x) <= 0x0fffffffeUL) == (x <= 0xfffffffe); }

int zext_geu_q(unsigned x) { return (zext(x) >= 0x100000001UL) == 0; }
int zext_geu_p(unsigned x) { return (zext(x) >= 0x100000000UL) == 0; }
int zext_geu_0(unsigned x) { return (zext(x) >= 0x0ffffffffUL) == (x >= 0xffffffff); }
int zext_geu_m(unsigned x) { return (zext(x) >= 0x0fffffffeUL) == (x >= 0xfffffffe); }

int zext_gtu_q(unsigned x) { return (zext(x) >  0x100000001UL) == 0; }
int zext_gtu_p(unsigned x) { return (zext(x) >  0x100000000UL) == 0; }
int zext_gtu_0(unsigned x) { return (zext(x) >  0x0ffffffffUL) == 0; }
int zext_gtu_m(unsigned x) { return (zext(x) >  0x0fffffffeUL) == (x >  0xfffffffe); }

/*
 * check-name: cmp-zext-uimm2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
