void bad0(void)
{
	int *a;
	*a++;
}

/*
 * check-name: undef00
 * check-command: test-linearize -Wno-decl -fdump-ir=mem2reg $file
 * check-known-to-fail
 * check-output-ignore
 * check-output-pattern(1): load\\.
 * check-output-pattern(1): load\\..*\\[UNDEF\\]
 */
