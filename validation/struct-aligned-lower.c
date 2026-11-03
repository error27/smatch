struct a { char c; long long x; } __attribute__((aligned(4)));
struct b { int i; char d __attribute__((aligned(64))); } __attribute__((aligned(8)));
union u { char c; long long x; } __attribute__((aligned(2)));
struct p { char c; int i; } __attribute__((packed, aligned(2)));

_Static_assert(sizeof(struct a) == 16 && _Alignof(struct a) == 8, "a");
_Static_assert(sizeof(struct b) == 128 && _Alignof(struct b) == 64, "b");
_Static_assert(__builtin_offsetof(struct b, d) == 64, "b.d");
_Static_assert(sizeof(union u) == 8 && _Alignof(union u) == 8, "u");
_Static_assert(sizeof(struct p) == 6 && _Alignof(struct p) == 2, "p");

/*
 * check-name: aligned() cannot lower the alignment of a struct
 */
