static int foo(long long a)
{
	return 0LL < (0x80000000LL + (a - a));
}

/*
 * check-name: cmp-type0
 * check-command: test-linearize $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
