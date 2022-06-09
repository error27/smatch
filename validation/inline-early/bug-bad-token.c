inline void fun(int x)
{
	(typeof(@)) x;
}

void foo(void)
{
	fun;
}

/*
 * check-name: bug-bad-token
 * check-exit-value: 0
 * check-error-ignore
 */
