typedef long long __attribute__((aligned(4))) c64;

struct a { char c; long long x __attribute__((aligned(4))); };
struct b { char c; long long x __attribute__((packed, aligned(4))); };
struct c { char c; long long x __attribute__((aligned(4), packed)); };
struct d { char c; c64 x; };
struct g { char c; c64 x __attribute__((aligned(2))); };
struct h { char c; c64 x __attribute__((aligned(4))); };
struct e { char c; long long x[2] __attribute__((aligned(4))); };
union u { char c; long long x __attribute__((aligned(4))); };

_Static_assert(sizeof(struct a) == 16 && _Alignof(struct a) == 8, "a");
_Static_assert(__builtin_offsetof(struct a, x) == 8, "a.x");
_Static_assert(sizeof(struct b) == 12 && _Alignof(struct b) == 4, "b");
_Static_assert(__builtin_offsetof(struct b, x) == 4, "b.x");
_Static_assert(sizeof(struct c) == 12 && _Alignof(struct c) == 4, "c");
_Static_assert(__builtin_offsetof(struct c, x) == 4, "c.x");
_Static_assert(sizeof(struct d) == 12 && _Alignof(struct d) == 4, "d");
_Static_assert(sizeof(struct g) == 12 && _Alignof(struct g) == 4, "g");
_Static_assert(sizeof(struct h) == 12 && _Alignof(struct h) == 4, "h");
_Static_assert(sizeof(struct e) == 24 && _Alignof(struct e) == 8, "e");
_Static_assert(__builtin_offsetof(struct e, x) == 8, "e.x");
_Static_assert(sizeof(union u) == 8 && _Alignof(union u) == 8, "u");

/*
 * check-name: aligned() cannot lower the alignment of a member
 */
