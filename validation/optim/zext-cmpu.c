int ltg(unsigned x) { return (((long long)x) <  0x100000000ULL) == 1; }
int ltl(unsigned x) { return (((long long)x) <  0x0ffffffffULL) == (x <  0xffffffffU); }
int leg(unsigned x) { return (((long long)x) <= 0x0ffffffffULL) == 1; }
int lel(unsigned x) { return (((long long)x) <= 0x0fffffffeULL) == (x <= 0xfffffffeU); }
int geg(unsigned x) { return (((long long)x) >= 0x100000000ULL) == 0; }
int gel(unsigned x) { return (((long long)x) >= 0x0ffffffffULL) == (x >= 0xffffffffU); }
int gtg(unsigned x) { return (((long long)x) >  0x0ffffffffULL) == 0; }
int gtl(unsigned x) { return (((long long)x) >  0x0fffffffeULL) == (x >  0xfffffffeU); }

/*
 * check-name: zext-cmpu
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
