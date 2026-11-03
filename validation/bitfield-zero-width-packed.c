struct a { char c; int :0; char d; } __attribute__((packed));
struct b { char c:3; int :0; char d; } __attribute__((packed));
struct c { char c; long long :0; char d; } __attribute__((packed));
struct d { char c; short :0; char d; } __attribute__((packed));
struct e { char c; int x:4; int :0; char d; } __attribute__((packed));
struct f { char c; char :0; char d; } __attribute__((packed));
#pragma pack(2)
struct g { char c; int :0; char d; };
struct h { char c; long long :0; char d; };
#pragma pack(1)
struct i { char c; int :0; char d; };
#pragma pack()
struct j { char c; int :0; char d; };
struct k { char c; int :0; char d; } __attribute__((packed, aligned(2)));
struct l { int :0; char d; } __attribute__((packed));
_Static_assert(sizeof(struct a) == 5 && _Alignof(struct a) == 1 && __builtin_offsetof(struct a, d) == 4, "a");
_Static_assert(sizeof(struct b) == 5 && _Alignof(struct b) == 1 && __builtin_offsetof(struct b, d) == 4, "b");
_Static_assert(sizeof(struct c) == 9 && _Alignof(struct c) == 1 && __builtin_offsetof(struct c, d) == 8, "c");
_Static_assert(sizeof(struct d) == 3 && _Alignof(struct d) == 1 && __builtin_offsetof(struct d, d) == 2, "d");
_Static_assert(sizeof(struct e) == 5 && _Alignof(struct e) == 1 && __builtin_offsetof(struct e, d) == 4, "e");
_Static_assert(sizeof(struct f) == 2 && _Alignof(struct f) == 1 && __builtin_offsetof(struct f, d) == 1, "f");
_Static_assert(sizeof(struct g) == 5 && _Alignof(struct g) == 1 && __builtin_offsetof(struct g, d) == 4, "g");
_Static_assert(sizeof(struct h) == 9 && _Alignof(struct h) == 1 && __builtin_offsetof(struct h, d) == 8, "h");
_Static_assert(sizeof(struct i) == 5 && _Alignof(struct i) == 1 && __builtin_offsetof(struct i, d) == 4, "i");
_Static_assert(sizeof(struct j) == 5 && _Alignof(struct j) == 1 && __builtin_offsetof(struct j, d) == 4, "j");
_Static_assert(sizeof(struct k) == 6 && _Alignof(struct k) == 2 && __builtin_offsetof(struct k, d) == 4, "k");
_Static_assert(sizeof(struct l) == 1 && _Alignof(struct l) == 1 && __builtin_offsetof(struct l, d) == 0, "l");

/*
 * check-name: zero-width bitfields in packed structs
 * check-command: sparse --arch=x86_64 $file
 */
