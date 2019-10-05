#define __packed	__attribute__((packed))

struct s {
	unsigned int f0:1;
	unsigned int f1:1;
	unsigned int pad:6;
} __packed;
_Static_assert(sizeof(struct s) == 1,  "sizeof(struct s)");

extern struct s g;

static int foo(struct s *ptr)
{
	int f = 0;

	f += g.f0;
	f += g.f1;

	f += ptr->f0;
	f += ptr->f1;

	return f;
}

/*
 * check-name: packed-bitfield1
 */
