extern int get(void);

static int array[8192];

static int foo(void)
{
	int n = -1;
	if (n < 0)
		n = get();
	return array[n];
}

/*
 * check-name: cmp-op-type
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 */
