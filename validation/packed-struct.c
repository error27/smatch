#define __packed __attribute__((packed))

typedef unsigned char   u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

struct a {
	u8 a;
	u8 b;
	u16 c;
} __packed;
_Static_assert(__alignof(struct a) == 1, "align struct");
_Static_assert(   sizeof(struct a) == 4, " size struct");

struct b {
	u32	a;
	u32	b;
} __packed;
_Static_assert(__alignof(struct b) == 1, "align struct");
_Static_assert(   sizeof(struct b) == 8, "size struct");

struct c {
	u16	a;
	u32	b;
} __packed;
_Static_assert(__alignof(struct c) == 1, "align struct");
_Static_assert(   sizeof(struct c) == 6, "size struct");

/*
 * check-name: packed-struct
 */
