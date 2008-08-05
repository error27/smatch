static int foo(int a)
{
	return a ?? 1 : 0;
}
/*
 * check-name: Bad ternary syntax
 * check-description: Once caused Sparse to segfault
 * check-error-start
bad-ternary-cond.c:3:12: error: Expected : in conditional expression
bad-ternary-cond.c:3:12: error: got ?
 * check-error-end
 */
