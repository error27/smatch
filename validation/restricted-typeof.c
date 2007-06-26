typedef unsigned __attribute__((bitwise)) A;
static A x;
static __typeof__(x) y;
static A *p = &y;
