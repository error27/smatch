struct b { char c; int i; };
_Static_assert(sizeof(struct b) == 8, "b");

/* Left active at the end of the file */
#pragma pack(push, 1)
struct a { char c; int i; };
_Static_assert(sizeof(struct a) == 5, "a");

/*
 * check-name: pragma-pack-reset
 * check-command: sparse $file $file
 */
