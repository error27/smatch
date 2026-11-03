#define N 1

#pragma pack(push, 1)
struct a { char c; int i; };
#pragma pack(push, lbl, 2)
struct b { char c; int i; };
struct c { char c; int i __attribute__((aligned(8))); };
struct d { char c; int i; } __attribute__((aligned(8)));
union u { char c[3]; int i; };
#pragma pack(push, 4)
#pragma pack(pop, lbl)
struct e { char c; int i; };
#pragma pack(pop)
struct f { char c; int i; };
#pragma pack(push, N)
struct g { char c; int i; };
#pragma pack(pop)
struct h { char c; int i;
#pragma pack(1)
};
#pragma pack()
#pragma pack(push, 1)
struct i { int a:12; short b:14; char c:5; };
#pragma pack(4)
struct j { int a:24; int b:27; char c:6; int d[2]; };
#pragma pack(pop)

int fn(void)
{
#pragma pack(push, 2)
	struct l { char c; int i; };
#pragma pack(pop)
	return sizeof(struct l);
}

_Static_assert(sizeof(struct a) == 5 && _Alignof(struct a) == 1, "a");
_Static_assert(sizeof(struct b) == 6 && _Alignof(struct b) == 2, "b");
_Static_assert(sizeof(struct c) == 6 && _Alignof(struct c) == 2, "c");
_Static_assert(__builtin_offsetof(struct c, i) == 2, "c.i");
_Static_assert(sizeof(struct d) == 8 && _Alignof(struct d) == 8, "d");
_Static_assert(__builtin_offsetof(struct d, i) == 2, "d.i");
_Static_assert(sizeof(union u) == 4 && _Alignof(union u) == 2, "u");
_Static_assert(sizeof(struct e) == 5, "e");
_Static_assert(sizeof(struct f) == 8, "f");
_Static_assert(sizeof(struct g) == 8, "g");
_Static_assert(sizeof(struct h) == 5, "h");
_Static_assert(sizeof(struct i) == 4 && _Alignof(struct i) == 1, "i");
_Static_assert(sizeof(struct j) == 16 && _Alignof(struct j) == 4, "j");
_Static_assert(__builtin_offsetof(struct j, d) == 8, "j.d");

/*
 * check-name: pragma-pack
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: ret\\.32 *\\$6
 */
