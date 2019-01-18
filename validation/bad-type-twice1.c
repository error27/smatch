static unsigned long foo(unsigned long val, void *ref)
{
	if (val >= ref)
		val = 0;
	return val;
}

/*
 * check-name: bad-type-twice1
 *
 * check-error-start
bad-type-twice1.c:3:17: error: incompatible types for operation (>=):
bad-type-twice1.c:3:17:    unsigned long val
bad-type-twice1.c:3:17:    void *ref
 * check-error-end
 */
