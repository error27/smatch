#define N	1024

_Bool check_ok(int i)
{
	return (i >= 0 && i < N) == (((unsigned int)i) < N);
}

/*
 * check-name: range-check2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-returns: 1
 */
