static inline void foo(void)
{
	foo();
}

static void baz(void)
{
	foo();
}

/*
 * check-name: inline_self
 */
