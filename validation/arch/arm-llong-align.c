struct s { char c; long long x; };
struct t { char c; long long :0; char d; };
#pragma pack(4)
struct u { char c; long long x; };
#pragma pack()
struct v { int i; long long x; } __attribute__((packed));
union w { char c; unsigned long long x; };

_Static_assert(_Alignof(long long) == 8, "long long");
_Static_assert(_Alignof(unsigned long long) == 8, "unsigned long long");
_Static_assert(_Alignof(double) == 8 && _Alignof(long double) == 8, "double");
_Static_assert(_Alignof(long) == 4 && _Alignof(void *) == 4, "long");
_Static_assert(sizeof(struct s) == 16 && _Alignof(struct s) == 8, "s");
_Static_assert(sizeof(struct t) == 16 && _Alignof(struct t) == 8, "t");
_Static_assert(__builtin_offsetof(struct t, d) == 8, "t.d");
_Static_assert(sizeof(struct u) == 12 && _Alignof(struct u) == 4, "u");
_Static_assert(sizeof(struct v) == 12 && _Alignof(struct v) == 1, "v");
_Static_assert(sizeof(union w) == 8 && _Alignof(union w) == 8, "w");

/*
 * check-name: arch/arm-llong-align
 * check-command: sparse --arch=arm $file
 */
