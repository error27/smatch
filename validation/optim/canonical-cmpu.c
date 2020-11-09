// canonicalize to == or !=
int cmp_ltu_eq0(unsigned int x) { return (x <  1) == (x == 0); }
int cmp_geu_ne0(unsigned int x) { return (x >= 1) == (x != 0); }

// canonicalize to the smaller value
int cmp_ltu(unsigned int x) { return (x <  256) == (x <= 255); }
int cmp_geu(unsigned int x) { return (x >= 256) == (x >  255); }

/*
 * check-name: canonical-cmpu
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
