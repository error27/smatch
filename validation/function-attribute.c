#define __pure __attribute__((pure))


static __pure int funi(int val)
{
	return val;
}

static __pure int *funp(int *ptr)
{
	return ptr;
}

static void foo(int val, int *ptr)
{
	int  nbr = funi(val);
	int *res = funp(ptr);
}

/*
 * check-name: function-attribute
 */
