#define __packed __attribute__((packed))

typedef unsigned char   u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

struct b {
	u32	a:1;
	u32	b:2;
	u32	c:4;
	u32	d:8;
	u32	e:16;
} __packed;
_Static_assert(__alignof(struct b) == 1);
_Static_assert(   sizeof(struct b) == 4);

struct c {
	u8	a;
	u8	b;
	u64	c:48;
} __packed;
_Static_assert(__alignof(struct c) == 1);
_Static_assert(   sizeof(struct c) == 8);

/*
 * check-name: packed-bitfield3
 */
