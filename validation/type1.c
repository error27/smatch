/*
 * We get this wrong for some strange reason.
 *
 * When evaluating the argument to the inline
 * function for the array, we don't properly
 * demote the "char []" to a "char *", but instead
 * we follow the dereference and get a "struct hello".
 *
 * Which makes no sense at all.
 */

static inline int deref(const char *s)
{
	return *s;
}

struct hello {
	char array[10];
};

static int test(struct hello *arg)
{
	return deref(arg->array);
}
