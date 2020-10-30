int foo(void)
{
	return 0;
}

/*
 * check-name: testsuite
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-match(ret): \\$0
 * check-output-returns: 0
 */
