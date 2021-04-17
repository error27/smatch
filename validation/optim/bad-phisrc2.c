int bad_phisrc2(int p, int a, int r)
{
	if (p)
		r = a;
	else if (r)
		;
	return r;
}

/*
 * check-name: bad-phisrc2
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-contains: select\\.
 */
