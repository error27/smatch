static void foo(void)
{
	unsigned short p = 0;
	int x = 1;

	for (;;)
		if (p)
			p = x;
}

/*
 * check-name: cse-size
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phi\\.
 * check-output-excludes: cbr
 */
