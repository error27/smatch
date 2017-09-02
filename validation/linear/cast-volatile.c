static int foo(volatile int *a, int v)
{
	*a = v;
	return *a;
}

/*
 * check-name: cast-volatile
 * check-command: test-linearize -fdump-ir=linearize $file
 *
 * check-known-to-fail
 * check-output-ignore
 * check-output-excludes: scast\\.
 */
