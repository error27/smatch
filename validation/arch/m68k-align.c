struct p { char c; void *p; };
struct f { char c; float f; };
struct d { char c; double d; };
struct l { char c; long double ld; };
struct fn { char c; void (*fn)(void); };

_Static_assert(_Alignof(void *) == 2 && _Alignof(void (*)(void)) == 2, "ptr");
_Static_assert(_Alignof(float) == 2 && _Alignof(double) == 2, "fp");
_Static_assert(_Alignof(long double) == 2 && sizeof(long double) == 12, "ld");
_Static_assert(sizeof(struct p) == 6 && _Alignof(struct p) == 2, "p");
_Static_assert(sizeof(struct f) == 6 && _Alignof(struct f) == 2, "f");
_Static_assert(sizeof(struct d) == 10 && _Alignof(struct d) == 2, "d");
_Static_assert(sizeof(struct l) == 14 && _Alignof(struct l) == 2, "l");
_Static_assert(sizeof(struct fn) == 6 && _Alignof(struct fn) == 2, "fn");

/*
 * check-name: arch/m68k-align
 * check-command: sparse --arch=m68k $file
 */
