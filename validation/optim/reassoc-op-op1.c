int foo(int x, int *ptr)
{
	int t = x + 1;
	*ptr = t;
	return t + -1;
}

/*
 * check-name: reassoc-op-op1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(1): add\\.
 */
