static void foo(void)
{
	unsigned p = 0;
	long x;

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
 * check-output-excludes: phisrc\\.
 */
