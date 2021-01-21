#define __packed __attribute__((packed))

typedef __UINT32_TYPE__ u32;

struct s {
	u32	a:5;
	u32	f:30;
	u32	z:5;
} __packed;
_Static_assert(sizeof(struct s) == 5);

static int ld(struct s *s)
{
	return s->f;
}

/*
 * check-name: packed-bitfield5
 * check-description: is check_access() OK with 'overlapping' packed bitfields?
 */
