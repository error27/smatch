void foo(int *ptr, int p)
{
	if (p)
		*ptr = 1;
	*ptr = 0;
}

/*
 * check-name: kill-dead-store-parent0
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(1): store
 */
