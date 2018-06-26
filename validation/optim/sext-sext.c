int foo(signed char offset)
{
	return (int)(short) offset;
}

/*
 * check-name: sext-sext
 * check-command: test-linearize -Wno-decl $file
 * check-known-to-fail
 *
 * check-output-ignore
 * check-output-pattern(1): sext\\.
 */
