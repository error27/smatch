typedef unsigned __attribute__((bitwise)) A;
A x;
__typeof__(x) y;
A *p = &y;
