static int bad_scope(void)
{
	int r = 0;

	for (int i = 0; i < 10; i++) {
		r = i;
	}

	return i;			/* check-should-fail */
}

/*
 * check-name: C99 for-loop declarations
 *
 * check-error-start
c99-for-loop-decl.c:9:16: error: undefined identifier 'i'
 * check-error-end
 */
