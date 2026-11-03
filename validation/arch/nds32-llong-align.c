#define LLA 8

struct s { char c; long long x; };
#pragma pack(4)
struct u { char c; long long x; };
#pragma pack()
struct v { int i; long long x; } __attribute__((packed));
union w { char c; unsigned long long x; };

_Static_assert(_Alignof(long long) == LLA, "long long");
_Static_assert(_Alignof(unsigned long long) == LLA, "unsigned long long");
_Static_assert(_Alignof(long) == 4 && _Alignof(void *) == 4, "long");
_Static_assert(sizeof(struct s) == 8 + LLA && _Alignof(struct s) == LLA, "s");
_Static_assert(sizeof(struct u) == 12 && _Alignof(struct u) == 4, "u");
_Static_assert(sizeof(struct v) == 12 && _Alignof(struct v) == 1, "v");
_Static_assert(sizeof(union w) == 8 && _Alignof(union w) == LLA, "w");

/*
 * check-name: arch/nds32-llong-align
 * check-command: sparse --arch=nds32 $file
 */
