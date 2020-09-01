extern int a, b;
inline int c(void) { return a++; }
inline int e(int d) { return 0; }
inline unsigned f(void) { return e(_Generic(b, int: c())); }
static int g(void) { return f(); }
static int h(void) { return f(); }

/*
 * check-name: inline-generic
 */
