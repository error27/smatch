enum a { A0, A1 = 255 } __attribute__((packed));
enum b { B0 = -1, B1 = 127 } __attribute__((packed));
enum c { C0, C1 = 256 } __attribute__((packed));
enum d { D0 = -129 } __attribute__((packed));
enum __attribute__((packed)) e { E0 = 0x10000 };
enum f { F0 = 0x100000000ULL } __attribute__((packed));
enum g { G0 };

_Static_assert(sizeof(enum a) == 1, "a");
_Static_assert(sizeof(enum b) == 1, "b");
_Static_assert(sizeof(enum c) == 2, "c");
_Static_assert(sizeof(enum d) == 2, "d");
_Static_assert(sizeof(enum e) == 4, "e");
_Static_assert(sizeof(enum f) == 8, "f");
_Static_assert(sizeof(enum g) == 4, "g");
_Static_assert(_Alignof(enum c) == 2, "align c");

/*
 * check-name: packed enums use the smallest integer type
 */
