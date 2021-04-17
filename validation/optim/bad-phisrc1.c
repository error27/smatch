void foo(int a, int b)
{
	if (b)
		while ((a += 5) > a)
			;
}

/*
 * check-name: bad-phisrc1
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-excludes: phi\\.
 * check-output-excludes: phisource\\.
 */
