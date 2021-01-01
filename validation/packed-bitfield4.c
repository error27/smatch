#define __packed __attribute__((packed))

typedef __UINT32_TYPE__ u32;

struct s {
	u32	f:24;
} __packed;
_Static_assert(sizeof(struct s) == 3);

static int ld(struct s *s)
{
	return s->f;
}

/*
 * check-name: packed-bitfield4
 * check-description: Is check_access() OK with short packed bitfields?
 */
