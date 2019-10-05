#define alignof(X)	__alignof__(X)
#define __packed	__attribute__((packed))

struct sa {
	int a:7;
	int c:10;
	int b:2;
} __packed;
_Static_assert(alignof(struct sa) == 1, "alignof(struct sa)");
_Static_assert( sizeof(struct sa) == 3,  "sizeof(struct sa)");


static int get_size(void)
{
	return sizeof(struct sa);
}

static void chk_align(struct sa sa, struct sa *p)
{
	_Static_assert(alignof(sa) == 1, "alignof(sa)");
	_Static_assert(alignof(*p) == 1, "alignof(*p)");
}

static int fp0(struct sa *sa)
{
	return sa->c;
}

static int fpx(struct sa *sa, int idx)
{
	return sa[idx].c;
}

static int fglobal(void)
{
	extern struct sa g;
	return g.c;
}

static struct sa l;
static int flocal(void)
{
	return l.c;
}


int main(void)
{
	extern void fun(struct sa *);
	struct sa sa = { 0 };

	fun(&sa);
	return 0;
}

/*
 * check-name: packed-bitfield0
 */
