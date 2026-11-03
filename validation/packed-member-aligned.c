struct storage { long long l; char d[120]; };
typedef struct { unsigned char b[16]; } guid_t;
typedef guid_t efi_guid_t __attribute__((aligned(4)));

struct req { int ifc; struct storage grp __attribute__((aligned(4))); } __attribute__((packed));
struct path { unsigned char t; efi_guid_t g; } __attribute__((packed));
struct up { char c; int i __attribute__((aligned(8))); } __attribute__((packed));
struct plain { char c; int i; } __attribute__((packed));
struct low { char c; long long x __attribute__((aligned(4))); } __attribute__((packed));

_Static_assert(sizeof(struct req) == 132 && _Alignof(struct req) == 4, "req");
_Static_assert(__builtin_offsetof(struct req, grp) == 4, "req.grp");
_Static_assert(sizeof(struct path) == 17 && _Alignof(struct path) == 1, "path");
_Static_assert(__builtin_offsetof(struct path, g) == 1, "path.g");
_Static_assert(sizeof(struct up) == 16 && _Alignof(struct up) == 8, "up");
_Static_assert(__builtin_offsetof(struct up, i) == 8, "up.i");
_Static_assert(sizeof(struct plain) == 5 && _Alignof(struct plain) == 1, "plain");
_Static_assert(sizeof(struct low) == 12 && _Alignof(struct low) == 4, "low");
_Static_assert(__builtin_offsetof(struct low, x) == 4, "low.x");

/*
 * check-name: aligned() members of packed structs
 */
