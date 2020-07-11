static int foo(int a)
{
	a = ;			// KO
	return a;
}

/*
 * check-name: empty-assign
 * check-known-to-fail
 *
 * check-error-start
empty-assign.c:3:11: error: expression expected before ';'
 * check-error-end
 */
