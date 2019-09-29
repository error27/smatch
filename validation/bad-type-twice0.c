static int foo(a)
{
	return a ? : 1;
}

/*
 * check-name: bad-type-twice0
 *
 * check-error-start
bad-type-twice0.c:3:16: error: non-scalar type in conditional:
bad-type-twice0.c:3:16:    incomplete type a
 * check-error-end
 */
