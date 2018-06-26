short seq(short x)
{
	return (int) x;
}

short ueq(unsigned short x)
{
	return (int) x;
}

/*
 * check-name: ext-trunc-same
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-excludes: trunc\\.
 * check-output-excludes: sext\\.
 * check-output-excludes: zext\\.
 */
