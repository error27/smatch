struct a { char c; int i; } __attribute__((aligned(4), packed));
struct b { char c; int i; } __attribute__((packed, aligned(4)));

_Static_assert(sizeof(struct a) == 8 && _Alignof(struct a) == 4, "a");
_Static_assert(__builtin_offsetof(struct a, i) == 1, "a.i");
_Static_assert(sizeof(struct b) == 8 && _Alignof(struct b) == 4, "b");
_Static_assert(__builtin_offsetof(struct b, i) == 1, "b.i");

/*
 * check-name: packed after aligned()
 */
