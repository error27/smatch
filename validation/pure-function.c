
static __attribute__((__pure__)) int pure_int(int v)
{
	int i = v;
	return i;
}

static __attribute__((__pure__)) void *pure_ptr(void *p)
{
    void *i = p;
    return i;
}

static void foo(int v, void *p)
{
	int   val = pure_int(v);
	void *ptr = pure_ptr(p);

	(void)val;
	(void)ptr;
}

/*
 * check-name: Pure function attribute
 */
