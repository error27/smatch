int fr_abx(int a, int b, int x) { return ((a & x) ^ (b & x)) == ((a ^ b) & x); }
int fl_abx(int a, int b, int x) { return ((x & a) ^ (x & b)) == ((a ^ b) & x); }
int fm_abx(int a, int b, int x) { return ((a & x) ^ (x & b)) == ((a ^ b) & x); }
int fn_abx(int a, int b, int x) { return ((x & a) ^ (b & x)) == ((a ^ b) & x); }

int fr_bax(int b, int a, int x) { return ((a & x) ^ (b & x)) == ((b ^ a) & x); }
int fl_bax(int b, int a, int x) { return ((x & a) ^ (x & b)) == ((b ^ a) & x); }
int fm_bax(int b, int a, int x) { return ((a & x) ^ (x & b)) == ((b ^ a) & x); }
int fn_bax(int b, int a, int x) { return ((x & a) ^ (b & x)) == ((b ^ a) & x); }

int fr_axb(int a, int x, int b) { return ((a & x) ^ (b & x)) == ((a ^ b) & x); }
int fl_axb(int a, int x, int b) { return ((x & a) ^ (x & b)) == ((a ^ b) & x); }
int fm_axb(int a, int x, int b) { return ((a & x) ^ (x & b)) == ((a ^ b) & x); }
int fn_axb(int a, int x, int b) { return ((x & a) ^ (b & x)) == ((a ^ b) & x); }

int fr_bxa(int b, int x, int a) { return ((b & x) ^ (a & x)) == ((b ^ a) & x); }
int fl_bxa(int b, int x, int a) { return ((x & b) ^ (x & a)) == ((b ^ a) & x); }
int fm_bxa(int b, int x, int a) { return ((b & x) ^ (x & a)) == ((b ^ a) & x); }
int fn_bxa(int b, int x, int a) { return ((x & b) ^ (a & x)) == ((b ^ a) & x); }

/*
 * check-name: fact-xor-and
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
