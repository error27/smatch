static int foo(a)
{
	return a ? : 1;
}

/*
 * check-name: bad-type-twice0
 *
 * check-error-start
bad-type-twice0.c:1:16: error: missing type declaration for parameter 'a'
 * check-error-end
 */
