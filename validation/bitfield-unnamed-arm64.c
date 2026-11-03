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
_Static_assert(sizeof(struct a) == 8 && _Alignof(struct a) == 4 && __builtin_offsetof(struct a, d) == 4, "a");
_Static_assert(sizeof(struct b) == 8 && _Alignof(struct b) == 4 && __builtin_offsetof(struct b, d) == 4, "b");
_Static_assert(sizeof(struct c) == 16 && _Alignof(struct c) == 8 && __builtin_offsetof(struct c, d) == 8, "c");
_Static_assert(sizeof(struct d) == 4 && _Alignof(struct d) == 2 && __builtin_offsetof(struct d, d) == 2, "d");
_Static_assert(sizeof(struct e) == 8 && _Alignof(struct e) == 4 && __builtin_offsetof(struct e, d) == 4, "e");
_Static_assert(sizeof(struct f) == 2 && _Alignof(struct f) == 1 && __builtin_offsetof(struct f, d) == 1, "f");
_Static_assert(sizeof(struct g) == 8 && _Alignof(struct g) == 4 && __builtin_offsetof(struct g, d) == 4, "g");
_Static_assert(sizeof(struct h) == 16 && _Alignof(struct h) == 8 && __builtin_offsetof(struct h, d) == 8, "h");
_Static_assert(sizeof(struct i) == 8 && _Alignof(struct i) == 4 && __builtin_offsetof(struct i, d) == 4, "i");
_Static_assert(sizeof(struct j) == 8 && _Alignof(struct j) == 4 && __builtin_offsetof(struct j, d) == 4, "j");
_Static_assert(sizeof(struct k) == 8 && _Alignof(struct k) == 4 && __builtin_offsetof(struct k, d) == 4, "k");
_Static_assert(sizeof(struct l) == 4 && _Alignof(struct l) == 4 && __builtin_offsetof(struct l, d) == 0, "l");
struct w_m { char c; int :3; };
struct w_n { char c; int :3; char d; };
struct w_o { char c; long long :0; };
struct w_p { int :0; };
struct w_q { char c; int :0; } __attribute__((packed));
_Static_assert(sizeof(struct w_m) == 4, "w_m size");
_Static_assert(_Alignof(struct w_m) == 4, "w_m align");
_Static_assert(sizeof(struct w_n) == 4, "w_n size");
_Static_assert(_Alignof(struct w_n) == 4, "w_n align");
_Static_assert(sizeof(struct w_o) == 8, "w_o size");
_Static_assert(_Alignof(struct w_o) == 8, "w_o align");
_Static_assert(sizeof(struct w_p) == 0, "w_p size");
_Static_assert(_Alignof(struct w_p) == 4, "w_p align");
_Static_assert(sizeof(struct w_q) == 4, "w_q size");
_Static_assert(_Alignof(struct w_q) == 4, "w_q align");

struct w2_r { char c; int :3; } __attribute__((packed));
struct w2_s { char c; long long :0; char d; } __attribute__((packed));
#pragma pack(2)
struct w2_t { char c; int :3; };
struct w2_u { char c; long long :0; char d; };
#pragma pack()
struct w2_v { char c; int x:3; } __attribute__((packed));
struct w2_w { char c; char :0; char d; };
_Static_assert(sizeof(struct w2_r) == 2, "w2_r size");
_Static_assert(_Alignof(struct w2_r) == 1, "w2_r align");
_Static_assert(sizeof(struct w2_s) == 16, "w2_s size");
_Static_assert(_Alignof(struct w2_s) == 8, "w2_s align");
_Static_assert(sizeof(struct w2_t) == 2, "w2_t size");
_Static_assert(_Alignof(struct w2_t) == 2, "w2_t align");
_Static_assert(sizeof(struct w2_u) == 16, "w2_u size");
_Static_assert(_Alignof(struct w2_u) == 8, "w2_u align");
_Static_assert(sizeof(struct w2_v) == 2, "w2_v size");
_Static_assert(_Alignof(struct w2_v) == 1, "w2_v align");
_Static_assert(sizeof(struct w2_w) == 2, "w2_w size");
_Static_assert(_Alignof(struct w2_w) == 1, "w2_w align");


/*
 * check-name: unnamed bitfields align structs on arm64
 * check-command: sparse --arch=arm64 $file
 */
