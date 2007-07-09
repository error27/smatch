static int a[] = {
	[0] = 0,		// OK
	[\0] = 1,		// KO
};
/*
 * check-name: Bad array designated initializer
 * check-command: sparse $file
 * check-exit-value: 1
 *
 * check-output-start
bad-array-designated-initializer.c:3:3: error: Expected constant expression
bad-array-designated-initializer.c:3:3: error: Expected } at end of initializer
bad-array-designated-initializer.c:3:3: error: got \
 * check-output-end
 *
 * check-known-to-fail
 */
