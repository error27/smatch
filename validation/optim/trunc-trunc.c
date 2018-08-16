char foo(int a)
{
	return ((((short) a) + 1) - 1);
}

/*
 * check-name: trunc-trunc
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-pattern(1): trunc\\.
 */
