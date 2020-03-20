int fun(void);

static void foo(void)
{
	__context__(1);
	if (!fun()) {
		__builtin_unreachable();
		return;
	}
	__context__(-1);
}

/*
 * check-name: context-unreachable
 */
