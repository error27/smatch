int foo(void)
{
	int r;
	long n;
	n = 0;
	return n < 2147483648U;
}

/*
 * check-name: cmp-type1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
