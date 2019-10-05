#define	__packed	__attribute__((packed))

typedef struct {
	__INT8_TYPE__	a;
	__INT16_TYPE__	b;
	__INT32_TYPE__	c;
} __packed obj_t;

_Static_assert(sizeof(obj_t) == 7, "sizeof packed struct");

static void foo(obj_t *ptr, int val)
{
	ptr->c = val;
}

static void bar(obj_t o)
{
	foo(&o, 0);
}

/*
 * check-name: packed-deref0
 */
