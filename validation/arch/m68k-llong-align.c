struct s { char c; long long x; };
struct t { char c; int i; };
union w { char c; unsigned long long x; };

_Static_assert(_Alignof(long long) == 2, "long long");
_Static_assert(_Alignof(unsigned long long) == 2, "unsigned long long");
_Static_assert(_Alignof(int) == 2 && _Alignof(long) == 2, "int");
_Static_assert(sizeof(struct s) == 10 && _Alignof(struct s) == 2, "s");
_Static_assert(sizeof(struct t) == 6 && _Alignof(struct t) == 2, "t");
_Static_assert(sizeof(union w) == 8 && _Alignof(union w) == 2, "w");

/*
 * check-name: arch/m68k-llong-align
 * check-command: sparse --arch=m68k $file
 */
